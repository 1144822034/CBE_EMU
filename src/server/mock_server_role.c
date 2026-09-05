static u32 vm_net_mock_load_auto_monster_catalog_dsh(const char *path)
{
    static u8 data[16384];
    u32 len = vm_net_mock_load_response_file(path, data, sizeof(data));
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 pos = 16;
    u32 added = 0;

    if (len < 16)
        return 0;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    if (columnCount < 4 || columnCount > 16 || rowCount > 512)
        return 0;

    for (u32 i = 0; i < columnCount; ++i)
    {
        u32 fieldLen = 0;
        if (pos >= len)
            return added;
        fieldLen = data[pos++];
        if (pos + fieldLen > len)
            return added;
        pos += fieldLen;
    }

    for (u32 row = 0; row < rowCount && pos + 4 <= len; ++row)
    {
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowPos = pos + 4;
        u32 rowEnd = rowPos + rowLen;
        const u8 *scene = NULL;
        u32 sceneLen = 0;
        u32 monsterIds[3] = {0, 0, 0};

        if (rowEnd > len || rowEnd < rowPos)
            break;

        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;

            if (rowPos + valueLen > rowEnd)
                break;
            if (col == 0)
            {
                scene = value;
                sceneLen = valueLen;
            }
            else if (col >= 1 && col <= 3)
            {
                monsterIds[col - 1] = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            }
            rowPos += valueLen;
        }

        if (vm_net_mock_add_auto_monster_catalog_item(scene, sceneLen,
                                                      monsterIds[0],
                                                      monsterIds[1],
                                                      monsterIds[2]))
        {
            ++added;
        }
        pos = rowEnd;
    }

    return added;
}

static bool g_vm_net_mock_scene_monster_override_flags[
    VM_NET_MOCK_AUTO_MONSTER_CATALOG_MAX_ITEMS];

static bool vm_net_mock_monster_enemy_id_known(u32 enemyId);

static bool vm_net_mock_scene_monster_db_row(void *contextValue,
                                             unsigned int columnCount,
                                             const char *const *values,
                                             const size_t *lengths)
{
    u32 *loaded = (u32 *)contextValue;
    char scene[64];
    size_t sceneLen = 0;
    u32 monsterIds[3] = {0, 0, 0};
    bool matched = false;

    memset(scene, 0, sizeof(scene));
    if (columnCount != 4 || values == NULL || lengths == NULL)
        return false;
    if (!vm_mysql_hex_decode(values[0], lengths[0], scene, sizeof(scene) - 1,
                             &sceneLen) ||
        sceneLen == 0 ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &monsterIds[0]) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &monsterIds[1]) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &monsterIds[2]) ||
        (monsterIds[0] == 0 && monsterIds[1] == 0 && monsterIds[2] == 0))
    {
        return true;
    }
    scene[sceneLen] = 0;
    for (u32 i = 0; i < g_vm_net_mock_auto_monster_catalog_count; ++i)
    {
        if (!vm_net_mock_scene_names_equal_loose(
                scene, g_vm_net_mock_auto_monster_catalog[i].scene))
            continue;
        g_vm_net_mock_auto_monster_catalog[i].monsterIds[0] = monsterIds[0];
        g_vm_net_mock_auto_monster_catalog[i].monsterIds[1] = monsterIds[1];
        g_vm_net_mock_auto_monster_catalog[i].monsterIds[2] = monsterIds[2];
        g_vm_net_mock_scene_monster_override_flags[i] = true;
        matched = true;
        break;
    }
    if (!matched &&
        vm_net_mock_add_auto_monster_catalog_item(
            (const u8 *)scene, (u32)sceneLen, monsterIds[0], monsterIds[1],
            monsterIds[2]))
    {
        u32 index = g_vm_net_mock_auto_monster_catalog_count - 1u;
        if (index < VM_NET_MOCK_AUTO_MONSTER_CATALOG_MAX_ITEMS)
            g_vm_net_mock_scene_monster_override_flags[index] = true;
        matched = true;
    }
    if (matched && loaded != NULL)
        ++(*loaded);
    return true;
}

static u32 vm_net_mock_load_scene_monster_overrides(void)
{
    u32 loaded = 0;

    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_scene_monsters ("
            "scene VARBINARY(63) NOT NULL,"
            "monster_id_1 SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "monster_id_2 SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "monster_id_3 SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(scene)) ENGINE=InnoDB"))
    {
        printf("[warn][mock-admin] scene_monster_table_create failed error=%s\n",
               vm_mysql_last_error());
        return 0;
    }
    if (!vm_mysql_query(
            "SELECT HEX(scene),monster_id_1,monster_id_2,monster_id_3 "
            "FROM server_scene_monsters",
            vm_net_mock_scene_monster_db_row, &loaded))
    {
        printf("[warn][mock-admin] scene_monster_load failed error=%s\n",
               vm_mysql_last_error());
        return 0;
    }
    if (loaded != 0)
    {
        printf("[info][mock-admin] scene_monster_overrides loaded=%u catalog=%u\n",
               loaded, g_vm_net_mock_auto_monster_catalog_count);
    }
    return loaded;
}

static u32 vm_net_mock_load_auto_monster_catalog(void)
{
    u32 added = 0;

    if (g_vm_net_mock_auto_monster_catalog_loaded)
        return g_vm_net_mock_auto_monster_catalog_count;

    g_vm_net_mock_auto_monster_catalog_loaded = true;
    g_vm_net_mock_auto_monster_catalog_count = 0;
    memset(g_vm_net_mock_scene_monster_override_flags, 0,
           sizeof(g_vm_net_mock_scene_monster_override_flags));
    added = vm_net_mock_load_auto_monster_catalog_dsh("JHOnlineData/automonster.dsh");
    if (added == 0)
        added = vm_net_mock_load_auto_monster_catalog_dsh("bin/JHOnlineData/automonster.dsh");
    if (added == 0)
        added = vm_net_mock_load_auto_monster_catalog_dsh("web/fs/JHOnlineData/automonster.dsh");

    if (added == 0)
    {
        printf("[warn][network] mock_auto_monster_catalog missing source=automonster.dsh\n");
    }
    else
    {
        printf("[info][network] mock_auto_monster_catalog total=%u source=automonster.dsh\n",
               g_vm_net_mock_auto_monster_catalog_count);
    }
    (void)vm_net_mock_load_scene_monster_overrides();
    return g_vm_net_mock_auto_monster_catalog_count;
}

static bool vm_net_mock_scene_monster_admin_get(
    const char *scene, u32 monsterIds[3], bool *overriddenOut)
{
    u32 total = vm_net_mock_load_auto_monster_catalog();

    if (monsterIds != NULL)
    {
        monsterIds[0] = 0;
        monsterIds[1] = 0;
        monsterIds[2] = 0;
    }
    if (overriddenOut)
        *overriddenOut = false;
    if (scene == NULL || scene[0] == 0)
        return false;
    for (u32 i = 0; i < total; ++i)
    {
        if (!vm_net_mock_scene_names_equal_loose(
                scene, g_vm_net_mock_auto_monster_catalog[i].scene))
            continue;
        if (monsterIds != NULL)
        {
            monsterIds[0] = g_vm_net_mock_auto_monster_catalog[i].monsterIds[0];
            monsterIds[1] = g_vm_net_mock_auto_monster_catalog[i].monsterIds[1];
            monsterIds[2] = g_vm_net_mock_auto_monster_catalog[i].monsterIds[2];
        }
        if (overriddenOut)
            *overriddenOut = g_vm_net_mock_scene_monster_override_flags[i];
        return true;
    }
    return false;
}

static bool vm_net_mock_scene_monster_admin_save(
    const char *scene, u32 monsterId1, u32 monsterId2, u32 monsterId3,
    const char **errorOut)
{
    char sceneHex[160];
    char query[512];

    if (errorOut)
        *errorOut = "????????";
    if (scene == NULL || scene[0] == 0 ||
        (monsterId1 == 0 && monsterId2 == 0 && monsterId3 == 0))
        return false;
    if ((monsterId1 != 0 && !vm_net_mock_monster_enemy_id_known(monsterId1)) ||
        (monsterId2 != 0 && !vm_net_mock_monster_enemy_id_known(monsterId2)) ||
        (monsterId3 != 0 && !vm_net_mock_monster_enemy_id_known(monsterId3)))
    {
        if (errorOut)
            *errorOut = "?? ID ???????";
        return false;
    }
    if (vm_mysql_hex_encode(scene, strlen(scene), sceneHex, sizeof(sceneHex)) ==
        0)
    {
        if (errorOut)
            *errorOut = "???????";
        return false;
    }
    (void)vm_net_mock_load_auto_monster_catalog();
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_scene_monsters ("
            "scene VARBINARY(63) NOT NULL,"
            "monster_id_1 SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "monster_id_2 SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "monster_id_3 SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(scene)) ENGINE=InnoDB"))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_scene_monsters("
             "scene,monster_id_1,monster_id_2,monster_id_3) "
             "VALUES(UNHEX('%s'),%u,%u,%u) ON DUPLICATE KEY UPDATE "
             "monster_id_1=VALUES(monster_id_1),"
             "monster_id_2=VALUES(monster_id_2),"
             "monster_id_3=VALUES(monster_id_3)",
             sceneHex, monsterId1, monsterId2, monsterId3);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = "??????????";
        return false;
    }
    g_vm_net_mock_auto_monster_catalog_loaded = false;
    (void)vm_net_mock_load_auto_monster_catalog();
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] scene_monster_save scene=%s monsters=%u/%u/%u\n",
           scene, monsterId1, monsterId2, monsterId3);
    return true;
}

static bool vm_net_mock_scene_monster_admin_reset(const char *scene,
                                                  const char **errorOut)
{
    char sceneHex[160];
    char query[256];

    if (errorOut)
        *errorOut = "????";
    if (scene == NULL || scene[0] == 0 ||
        vm_mysql_hex_encode(scene, strlen(scene), sceneHex, sizeof(sceneHex)) ==
            0)
        return false;
    snprintf(query, sizeof(query),
             "DELETE FROM server_scene_monsters WHERE scene=UNHEX('%s')",
             sceneHex);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = "??????????";
        return false;
    }
    g_vm_net_mock_auto_monster_catalog_loaded = false;
    (void)vm_net_mock_load_auto_monster_catalog();
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] scene_monster_reset scene=%s\n", scene);
    return true;
}

/*
 * Town / city hubs forbid hangup and spar.  Evidence: automonster.dsh lists only
 * field maps (01桃花岛…28*, never c04临安府_*); live product blocks 临安 etc.
 * Scene keys: authored `c`+digit towns (c04临安府_06), or sMap aliases that
 * drop the leading `c` (`4临安府_06`).  Field maps keep a two-digit area id.
 */
static bool vm_net_mock_scene_is_city_hangup_forbidden(const char *scene)
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
    /* Alias without town "c": single ASCII digit then GBK (not NN field id). */
    if (leaf[0] >= '1' && leaf[0] <= '9')
    {
        second = (unsigned char)leaf[1];
        if (second >= 0x80u)
            return true;
    }
    return false;
}

static bool vm_net_mock_select_auto_monster_for_scene(const char *scene,
                                                      u32 *enemyIdOut,
                                                      const char **matchedSceneOut)
{
    u32 total = vm_net_mock_load_auto_monster_catalog();
    u32 overrideEnemyId = vm_net_mock_env_u32("CBE_HANGUP_BATTLE_ENEMY_ID", 0);

    if (enemyIdOut)
        *enemyIdOut = 0;
    if (matchedSceneOut)
        *matchedSceneOut = NULL;
    if (overrideEnemyId != 0)
    {
        if (enemyIdOut)
            *enemyIdOut = overrideEnemyId;
        if (matchedSceneOut)
            *matchedSceneOut = "env:CBE_HANGUP_BATTLE_ENEMY_ID";
        return true;
    }
    if (scene == NULL || scene[0] == 0)
        return false;

    for (u32 i = 0; i < total; ++i)
    {
        const vm_net_mock_auto_monster_catalog_item *item = &g_vm_net_mock_auto_monster_catalog[i];
        u32 choices[3] = {0, 0, 0};
        u32 choiceCount = 0;

        if (!vm_net_mock_scene_names_equal_loose(scene, item->scene))
            continue;
        for (u32 j = 0; j < 3; ++j)
        {
            if (item->monsterIds[j] != 0)
                choices[choiceCount++] = item->monsterIds[j];
        }
        if (choiceCount == 0)
            return false;
        if (enemyIdOut)
            *enemyIdOut = choices[g_schedulerTick % choiceCount];
        if (matchedSceneOut)
            *matchedSceneOut = item->scene;
        return true;
    }

    return false;
}

static bool vm_net_mock_scene_is_c00_penglai03(const char *scene)
{
    return scene != NULL &&
           (strcmp(scene, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33") == 0 ||
            strcmp(scene, "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x33\x2e\x73\x63\x65") == 0);
}

static bool vm_net_mock_scene_is_taohuadao01(const char *scene)
{
    return scene != NULL &&
           strcmp(scene, "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65") == 0;
}

static bool vm_net_mock_adjust_safe_player_pos_from_sce(const char *scene, u16 *x, u16 *y);
static bool vm_net_mock_get_scene_reasonable_spawn_from_sce(const char *scene,
                                                            u16 *xOut,
                                                            u16 *yOut,
                                                            u16 *entryIdOut);

static void vm_net_mock_adjust_safe_player_pos_for_scene(const char *scene, u16 *x, u16 *y)
{
    if (scene == NULL || x == NULL || y == NULL)
        return;

    /*
     * SCE edge_portal spawn_point marks the portal glyph/trigger approach, not
     * a good player restore point. Restoring exactly on those coordinates makes
     * the yellow portal marker appear under the player or under the bottom UI.
     */
    if (vm_net_mock_scene_is_penglai04(scene) &&
        *x >= 224 && *x <= 288 &&
        *y >= 256 && *y <= 304)
    {
        *x = 256;
        *y = 245;
        return;
    }

    if (vm_net_mock_scene_is_penglai03(scene) &&
        *x >= 64 && *x <= 144 &&
        *y >= 390)
    {
        *x = 105;
        *y = 360;
        return;
    }

    (void)vm_net_mock_adjust_safe_player_pos_from_sce(scene, x, y);
}

/* Near-origin coords are treated like 0: portal safe-gap math and map edges
 * leave the sprite in the top-left with no walkable path (runtime: role 158
 * c04???_05 at 9,29). */
static bool vm_net_mock_role_position_needs_edge_rescue(u16 x, u16 y)
{
    return x == 0 || y == 0 ||
           x < (u16)VM_NET_MOCK_SCENE_LANDING_SAFE_GAP ||
           y < (u16)VM_NET_MOCK_SCENE_LANDING_SAFE_GAP;
}

/*
 * True when the dream SCE leaf on disk carries map kind=3 (e_ghostfire).
 * Cold login into that SCE faults ParseMinfo for the production client
 * (debug-191169: lr=0x010136b3 / emu_err=11) even when host smoke passes.
 */
static bool vm_net_mock_role_dream_kind3_on_disk(const char *scene)
{
    static const char *kPaths[] = {
        "../web/fs/JHOnlineData/%s",
        "JHOnlineData/%s",
        "../web/fs/JHOnlineData/%s.sce",
        "JHOnlineData/%s.sce",
    };
    char path[320];
    u8 buf[512];
    size_t i = 0;

    if (scene == NULL || scene[0] != '2' || scene[1] != '9')
        return false;
    for (i = 0; i < sizeof(kPaths) / sizeof(kPaths[0]); ++i)
    {
        FILE *fp = NULL;
        size_t n = 0;
        size_t off = 0;

        snprintf(path, sizeof(path), kPaths[i], scene);
        fp = fopen(path, "rb");
        if (fp == NULL)
            continue;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        {
            if (n >= 11u)
            {
                for (off = 0; off + 11u <= n; ++off)
                {
                    if (memcmp(buf + off, "e_ghostfire", 11) == 0)
                    {
                        fclose(fp);
                        return true;
                    }
                }
            }
        }
        fclose(fp);
    }
    return false;
}

/* Relocate login out of dream/instance kind=3 shells so cold DF never
 * ParseMinfo's them; ?? instance enter still loads kind=3 for ??.
 * Also covers experimental twin 28????_01 (debug-0b451a redirect left
 * roles stuck there; relocate previously only matched 29*). */
static bool vm_net_mock_role_relocate_dream_kind3_login(
    vm_net_mock_role_state *role)
{
    static const char kOutdoor[] =
        "\x63\x30\x34\xc1\xd9\xb0\xb2\xb8\xae\x5f\x30\x36"; /* c04???_06 */
    static const char kFieldDreamTwin[] =
        "\x32\x38\xd2\xb0\xcd\xe2\xc3\xce\xb2\xe2\x5f\x30\x31"; /* 28????_01 */
    char from[64];
    u16 x = 50;
    u16 y = 50;
    u8 isDream29 = 0;
    u8 isFieldTwin = 0;

    if (role == NULL || role->scene[0] == 0)
        return false;
    isDream29 = (role->scene[0] == '2' && role->scene[1] == '9') ? 1u : 0u;
    isFieldTwin =
        (strcmp(role->scene, kFieldDreamTwin) == 0) ? 1u : 0u;
    if (!isDream29 && !isFieldTwin)
        return false;
    if (isDream29 && !vm_net_mock_role_dream_kind3_on_disk(role->scene))
        return false;
    snprintf(from, sizeof(from), "%s", role->scene);
    snprintf(role->scene, sizeof(role->scene), "%s", kOutdoor);
    if (!vm_net_mock_get_scene_reasonable_spawn_from_sce(role->scene, &x, &y,
                                                          NULL))
    {
        x = 50;
        y = 50;
    }
    vm_net_mock_adjust_safe_player_pos_for_scene(role->scene, &x, &y);
    role->x = x;
    role->y = y;
    g_vm_net_mock_role_position_dirty = true;
    printf("[info][mock-service] role_dream_kind3_login_relocate role=%u "
           "from=%s to=%s pos=(%u,%u) "
           "action=outdoor-login-keep-dream-kind3-for-instance "
           "evidence=debug-191169-cold-kind3-screen_init_fail\n",
           role->roleId, from, role->scene, role->x, role->y);
    if (isDream29)
        (void)vm_net_mock_dream_purge_item_841(role, "dream-login-relocate");
    return true;
}

static bool vm_net_mock_role_rescue_edge_position(vm_net_mock_role_state *role,
                                                  const char *reason)
{
    u16 beforeX = 0;
    u16 beforeY = 0;
    u16 x = 0;
    u16 y = 0;

    if (role == NULL || !vm_net_mock_scene_name_is_persistable(role->scene))
        return false;
    beforeX = role->x;
    beforeY = role->y;
    if (!vm_net_mock_role_position_needs_edge_rescue(beforeX, beforeY))
        return false;

    x = VM_NET_MOCK_ROLE_INITIAL_X;
    y = VM_NET_MOCK_ROLE_INITIAL_Y;
    if (!vm_net_mock_get_scene_reasonable_spawn_from_sce(role->scene, &x, &y, NULL))
    {
        x = VM_NET_MOCK_ROLE_INITIAL_X;
        y = VM_NET_MOCK_ROLE_INITIAL_Y;
    }
    vm_net_mock_adjust_safe_player_pos_for_scene(role->scene, &x, &y);
    if (vm_net_mock_role_position_needs_edge_rescue(x, y))
    {
        x = 64;
        y = 64;
        vm_net_mock_adjust_safe_player_pos_for_scene(role->scene, &x, &y);
    }
    if (x == 0)
        x = VM_NET_MOCK_ROLE_INITIAL_X;
    if (y == 0)
        y = VM_NET_MOCK_ROLE_INITIAL_Y;
    role->x = x;
    role->y = y;
    g_vm_net_mock_role_position_dirty = true;
    printf("[warn][mock-service] role_edge_pos_rescue role=%u scene=%s "
           "from=(%u,%u) to=(%u,%u) reason=%s evidence=near-origin-edge\n",
           role->roleId, role->scene, beforeX, beforeY, role->x, role->y,
           reason ? reason : "edge_rescue");
    return true;
}

static void vm_net_mock_role_db_path(char *path, size_t pathSize)
{
    char safeAccount[64];
    const char *accountId = g_vm_mock_service_active_account_id;
    size_t outPos = 0;

    if (path == NULL || pathSize == 0)
        return;
    path[0] = 0;
    if (accountId == NULL || accountId[0] == 0)
        return;

    memset(safeAccount, 0, sizeof(safeAccount));
    for (size_t i = 0; accountId[i] != 0 && outPos + 1 < sizeof(safeAccount); ++i)
    {
        unsigned char ch = (unsigned char)accountId[i];
        if ((ch >= '0' && ch <= '9') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            ch == '_' || ch == '-')
        {
            safeAccount[outPos++] = (char)ch;
        }
        else
        {
            safeAccount[outPos++] = '_';
        }
    }
    if (outPos == 0)
        return;
    snprintf(path, pathSize, "nvram/accounts/%s/jhol_mock_roles.bin", safeAccount);
}

static void vm_mock_service_account_db_path(char *path, size_t pathSize)
{
    if (path == NULL || pathSize == 0)
        return;
    snprintf(path, pathSize, "nvram/mock_service_accounts.bin");
}

static size_t vm_mock_mysql_bounded_strlen(const char *text, size_t capacity)
{
    size_t length = 0;
    if (text == NULL)
        return 0;
    while (length < capacity && text[length] != 0)
        ++length;
    return length;
}

static bool vm_mock_mysql_copy_text(char *destination,
                                    size_t destination_size,
                                    const char *value,
                                    size_t value_len)
{
    if (destination == NULL || destination_size == 0 || value == NULL || value_len >= destination_size)
        return false;
    memcpy(destination, value, value_len);
    destination[value_len] = 0;
    return true;
}

static bool vm_mock_mysql_parse_u32(const char *value, size_t value_len, u32 *result_out)
{
    uint64_t result = 0;
    if (value == NULL || value_len == 0 || result_out == NULL)
        return false;
    for (size_t i = 0; i < value_len; ++i)
    {
        if (value[i] < '0' || value[i] > '9')
            return false;
        result = result * 10u + (u32)(value[i] - '0');
        if (result > 0xffffffffu)
            return false;
    }
    *result_out = (u32)result;
    return true;
}

/*
 * MySQL is the sole authority once the initial legacy/payload import has
 * completed.  In particular, a relation row disappearing later must not be
 * interpreted as another invitation to resurrect an arbitrarily old binary
 * snapshot.  The seal is recorded in the database so a mock-service restart
 * cannot forget that the migration already happened.
 */
#define VM_MOCK_MYSQL_AUTHORITY_MIGRATION "mysql-authoritative-v1"

static bool g_vm_mock_mysql_authority_prepared = false;
static bool g_vm_mock_mysql_authority_sealed = false;

typedef struct
{
    u8 seenMask;
    bool invalid;
} vm_mock_mysql_authority_engine_context;

typedef struct
{
    bool found;
    bool invalid;
} vm_mock_mysql_authority_marker_context;

typedef struct
{
    u32 value;
    bool found;
    bool invalid;
} vm_mock_mysql_authority_count_context;

static bool vm_mock_mysql_authority_engine_row(void *context_value,
                                               unsigned int column_count,
                                               const char *const *values,
                                               const size_t *lengths)
{
    static const char *const table_names[] = {
        "accounts",
        "friendships",
        "account_role_state",
        "account_roles",
        "account_role_equipment",
        "account_role_backpack",
        "server_data_migrations"
    };
    vm_mock_mysql_authority_engine_context *context =
        (vm_mock_mysql_authority_engine_context *)context_value;
    u32 matched = sizeof(table_names) / sizeof(table_names[0]);

    if (context == NULL || column_count != 2 || values[0] == NULL || values[1] == NULL)
        goto invalid;
    for (u32 i = 0; i < sizeof(table_names) / sizeof(table_names[0]); ++i)
    {
        size_t name_len = strlen(table_names[i]);
        if (lengths[0] == name_len && memcmp(values[0], table_names[i], name_len) == 0)
        {
            matched = i;
            break;
        }
    }
    if (matched >= sizeof(table_names) / sizeof(table_names[0]) ||
        (context->seenMask & (1u << matched)) != 0 ||
        lengths[1] != 6 || memcmp(values[1], "InnoDB", 6) != 0)
        goto invalid;
    context->seenMask |= (u8)(1u << matched);
    return true;

invalid:
    if (context != NULL)
        context->invalid = true;
    return true;
}

static bool vm_mock_mysql_authority_marker_row(void *context_value,
                                                unsigned int column_count,
                                                const char *const *values,
                                                const size_t *lengths)
{
    vm_mock_mysql_authority_marker_context *context =
        (vm_mock_mysql_authority_marker_context *)context_value;
    size_t marker_len = strlen(VM_MOCK_MYSQL_AUTHORITY_MIGRATION);

    if (context == NULL || context->found || column_count != 1 || values[0] == NULL ||
        lengths[0] != marker_len ||
        memcmp(values[0], VM_MOCK_MYSQL_AUTHORITY_MIGRATION, marker_len) != 0)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_mock_mysql_authority_count_row(void *context_value,
                                               unsigned int column_count,
                                               const char *const *values,
                                               const size_t *lengths)
{
    vm_mock_mysql_authority_count_context *context =
        (vm_mock_mysql_authority_count_context *)context_value;

    if (context == NULL || context->found || column_count != 1 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->value))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_mock_service_mysql_authority_prepare(void)
{
    vm_mock_mysql_authority_engine_context engine_context;
    vm_mock_mysql_authority_marker_context marker_context;
    const u8 expected_mask = (u8)((1u << 7) - 1u);

    if (g_vm_mock_mysql_authority_prepared)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_data_migrations ("
            "migration_name VARCHAR(127) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "applied_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY(migration_name)) ENGINE=InnoDB"))
    {
        printf("[error][mock-service] mysql_authority_prepare schema error=%s\n",
               vm_mysql_last_error());
        return false;
    }

    memset(&engine_context, 0, sizeof(engine_context));
    if (!vm_mysql_query(
            "SELECT TABLE_NAME,ENGINE FROM information_schema.TABLES "
            "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN "
            "('accounts','friendships','account_role_state','account_roles',"
            "'account_role_equipment','account_role_backpack','server_data_migrations')",
            vm_mock_mysql_authority_engine_row, &engine_context) ||
        engine_context.invalid || engine_context.seenMask != expected_mask)
    {
        printf("[error][mock-service] mysql_authority_prepare engine "
               "required=InnoDB seen_mask=0x%02x expected_mask=0x%02x error=%s\n",
               engine_context.seenMask, expected_mask, vm_mysql_last_error());
        return false;
    }

    memset(&marker_context, 0, sizeof(marker_context));
    if (!vm_mysql_query(
            "SELECT migration_name FROM server_data_migrations "
            "WHERE migration_name='" VM_MOCK_MYSQL_AUTHORITY_MIGRATION "'",
            vm_mock_mysql_authority_marker_row, &marker_context) ||
        marker_context.invalid)
    {
        printf("[error][mock-service] mysql_authority_prepare marker error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_mock_mysql_authority_sealed = marker_context.found;
    g_vm_mock_mysql_authority_prepared = true;
    printf("[info][mock-service] mysql_authority_prepare sealed=%u "
           "storage=InnoDB marker=%s\n",
           g_vm_mock_mysql_authority_sealed ? 1u : 0u,
           VM_MOCK_MYSQL_AUTHORITY_MIGRATION);
    return true;
}

static bool vm_mock_service_mysql_authority_seal(void)
{
    if (!vm_mock_service_mysql_authority_prepare())
        return false;
    if (g_vm_mock_mysql_authority_sealed)
        return true;
    if (!vm_mysql_exec(
            "INSERT IGNORE INTO server_data_migrations(migration_name) VALUES('"
            VM_MOCK_MYSQL_AUTHORITY_MIGRATION "')"))
    {
        printf("[error][mock-service] mysql_authority_seal error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_mock_mysql_authority_sealed = true;
    printf("[info][mock-service] mysql_authority_seal marker=%s\n",
           VM_MOCK_MYSQL_AUTHORITY_MIGRATION);
    return true;
}

static bool vm_mock_service_mysql_authority_is_sealed(void)
{
    return g_vm_mock_mysql_authority_prepared && g_vm_mock_mysql_authority_sealed;
}

static bool vm_mock_service_mysql_has_role_data(bool *has_data_out)
{
    vm_mock_mysql_authority_count_context context;

    if (has_data_out)
        *has_data_out = false;
    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(
            "SELECT (SELECT COUNT(*) FROM account_role_state)+"
            "(SELECT COUNT(*) FROM account_roles)+"
            "(SELECT COUNT(*) FROM account_role_state_payload_backup)",
            vm_mock_mysql_authority_count_row, &context) ||
        context.invalid || !context.found)
    {
        return false;
    }
    if (has_data_out)
        *has_data_out = context.value != 0;
    return true;
}

typedef struct
{
    vm_mock_service_account_record *record;
    bool found;
    bool invalid;
} vm_mock_mysql_account_lookup_context;

typedef struct
{
    char *accountIdOut;
    size_t accountIdOutCap;
    bool found;
    bool invalid;
} vm_mock_mysql_account_id_context;

typedef bool (*vm_mock_service_account_id_callback)(const char *accountId, void *context);

typedef struct
{
    vm_mock_service_account_id_callback callback;
    void *context;
    bool failed;
    bool invalid;
} vm_mock_mysql_account_foreach_context;

static bool vm_mock_mysql_account_lookup_row(void *context_value,
                                             unsigned int column_count,
                                             const char *const *values,
                                             const size_t *lengths)
{
    vm_mock_mysql_account_lookup_context *context =
        (vm_mock_mysql_account_lookup_context *)context_value;
    size_t password_len = 0;

    if (context == NULL || context->record == NULL || context->found || column_count != 2)
        goto invalid;
    memset(context->record, 0, sizeof(*context->record));
    if (!vm_mock_mysql_copy_text(context->record->username, sizeof(context->record->username),
                                 values[0], lengths[0]) ||
        values[1] == NULL ||
        !vm_mysql_hex_decode(values[1], lengths[1], context->record->password,
                             sizeof(context->record->password) - 1, &password_len))
    {
        goto invalid;
    }
    context->record->password[password_len] = 0;
    context->found = true;
    return true;

invalid:
    if (context != NULL)
    {
        context->invalid = true;
        if (context->record != NULL)
            memset(context->record, 0, sizeof(*context->record));
    }
    return true;
}

static bool vm_mock_mysql_account_id_row(void *context_value,
                                          unsigned int column_count,
                                          const char *const *values,
                                          const size_t *lengths)
{
    vm_mock_mysql_account_id_context *context =
        (vm_mock_mysql_account_id_context *)context_value;

    if (context == NULL || context->found || column_count != 1 ||
        !vm_mock_mysql_copy_text(context->accountIdOut, context->accountIdOutCap,
                                 values[0], lengths[0]))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_mock_mysql_account_foreach_row(void *context_value,
                                              unsigned int column_count,
                                              const char *const *values,
                                              const size_t *lengths)
{
    vm_mock_mysql_account_foreach_context *context =
        (vm_mock_mysql_account_foreach_context *)context_value;
    char account_id[64];

    if (context == NULL || context->callback == NULL || column_count != 1 ||
        !vm_mock_mysql_copy_text(account_id, sizeof(account_id), values[0], lengths[0]))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    if (!context->callback(account_id, context->context))
    {
        context->failed = true;
        return false;
    }
    return true;
}

static bool vm_mock_service_account_count(u32 *countOut)
{
    vm_mock_mysql_authority_count_context context;

    if (countOut)
        *countOut = 0;
    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query("SELECT COUNT(*) FROM accounts",
                        vm_mock_mysql_authority_count_row, &context) ||
        context.invalid || !context.found)
    {
        return false;
    }
    if (countOut)
        *countOut = context.value;
    return true;
}

static bool vm_mock_service_account_first_id(char *accountIdOut, size_t accountIdOutCap)
{
    vm_mock_mysql_account_id_context context;

    if (accountIdOut == NULL || accountIdOutCap == 0)
        return false;
    accountIdOut[0] = 0;
    memset(&context, 0, sizeof(context));
    context.accountIdOut = accountIdOut;
    context.accountIdOutCap = accountIdOutCap;
    if (!vm_mysql_query("SELECT account_id FROM accounts ORDER BY account_id LIMIT 1",
                        vm_mock_mysql_account_id_row, &context) ||
        context.invalid)
    {
        return false;
    }
    return context.found;
}

static bool vm_mock_service_account_foreach_id(vm_mock_service_account_id_callback callback,
                                               void *context)
{
    vm_mock_mysql_account_foreach_context foreach_context;

    if (callback == NULL)
        return false;
    memset(&foreach_context, 0, sizeof(foreach_context));
    foreach_context.callback = callback;
    foreach_context.context = context;
    if (!vm_mysql_query("SELECT account_id FROM accounts ORDER BY account_id",
                        vm_mock_mysql_account_foreach_row, &foreach_context) ||
        foreach_context.invalid)
    {
        return false;
    }
    return !foreach_context.failed;
}

typedef struct
{
    char (*ids)[64];
    u32 count;
    u32 capacity;
    bool invalid;
} vm_mock_service_account_id_list;

static bool vm_mock_service_account_id_list_push(const char *accountId, void *context)
{
    vm_mock_service_account_id_list *list = (vm_mock_service_account_id_list *)context;
    char (*grown)[64];

    if (list == NULL || accountId == NULL || accountId[0] == 0)
    {
        if (list != NULL)
            list->invalid = true;
        return false;
    }
    if (list->count >= list->capacity)
    {
        u32 next_capacity = list->capacity == 0 ? 64u : list->capacity * 2u;
        grown = (char (*)[64])realloc(list->ids, (size_t)next_capacity * sizeof(*list->ids));
        if (grown == NULL)
        {
            list->invalid = true;
            return false;
        }
        list->ids = grown;
        list->capacity = next_capacity;
    }
    snprintf(list->ids[list->count], sizeof(list->ids[list->count]), "%s", accountId);
    ++list->count;
    return true;
}

static void vm_mock_service_account_id_list_free(vm_mock_service_account_id_list *list)
{
    if (list == NULL)
        return;
    free(list->ids);
    list->ids = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* Snapshot account ids first so callers can open nested MySQL work per row. */
static bool vm_mock_service_account_collect_ids(vm_mock_service_account_id_list *list)
{
    if (list == NULL)
        return false;
    memset(list, 0, sizeof(*list));
    if (!vm_mock_service_account_foreach_id(vm_mock_service_account_id_list_push, list) ||
        list->invalid)
    {
        vm_mock_service_account_id_list_free(list);
        return false;
    }
    return true;
}

static bool vm_mock_service_account_db_write_record(const vm_mock_service_account_record *record,
                                                    bool create,
                                                    const char *reason)
{
    char username_hex[sizeof(record->username) * 2 + 1];
    char password_hex[sizeof(record->password) * 2 + 1];
    char query[768];
    size_t username_len;
    size_t password_len;

    if (!g_vm_mock_service_account_db_valid || record == NULL)
        return false;
    username_len = vm_mock_mysql_bounded_strlen(record->username, sizeof(record->username));
    password_len = vm_mock_mysql_bounded_strlen(record->password, sizeof(record->password));
    if (username_len == 0 || username_len >= sizeof(record->username) ||
        password_len >= sizeof(record->password) ||
        vm_mysql_hex_encode(record->username, username_len, username_hex, sizeof(username_hex)) == 0 ||
        vm_mysql_hex_encode(record->password, password_len, password_hex, sizeof(password_hex)) == 0)
    {
        vm_autotest_note("mock_account_db_mysql_write_failed reason=%s operation=%s error=invalid-account-record\n",
                         reason ? reason : "state", create ? "insert" : "update");
        return false;
    }
    if (create)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO accounts(account_id,password_value) VALUES(CAST(X'%s' AS CHAR),X'%s')",
                 username_hex, password_hex);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "UPDATE accounts SET password_value=X'%s' WHERE account_id=CAST(X'%s' AS CHAR)",
                 password_hex, username_hex);
    }
    if (!vm_mysql_exec(query))
    {
        vm_autotest_note("mock_account_db_mysql_write_failed reason=%s operation=%s error=%s\n",
                         reason ? reason : "state", create ? "insert" : "update",
                         vm_mysql_last_error());
        return false;
    }
    vm_autotest_note("mock_account_db_mysql_write reason=%s operation=%s\n",
                     reason ? reason : "state", create ? "insert" : "update");
    return true;
}

/*
 * Stream the legacy JHA1 snapshot into MySQL one row at a time.  The old
 * process kept a million-slot static table; that bloated the PE image and is
 * no longer part of the runtime contract.
 */
static bool vm_mock_service_account_db_migrate_legacy(const char *reason, u32 *importedCountOut)
{
    char path[128];
    vm_mock_service_account_db_file_header header;
    FILE *fp;
    u32 imported = 0;

    if (importedCountOut)
        *importedCountOut = 0;
    if (!g_vm_mock_service_account_db_valid)
        return false;
    vm_mock_service_account_db_path(path, sizeof(path));
    fp = fopen(path, "rb");
    if (fp == NULL)
        return false;
    memset(&header, 0, sizeof(header));
    if (fread(&header, 1, sizeof(header), fp) != sizeof(header) ||
        memcmp(header.magic, "JHA1", 4) != 0 ||
        header.version != VM_MOCK_SERVICE_ACCOUNT_DB_VERSION ||
        header.accountCount > VM_MOCK_SERVICE_ACCOUNT_DB_MAX_ACCOUNTS)
    {
        fclose(fp);
        return false;
    }

    /*
     * accounts is a parent of persisted recharge orders.  Import must not
     * DELETE the table; upsert preserves existing parent rows.
     */
    if (!vm_mysql_exec("START TRANSACTION"))
    {
        char mysql_error[512];
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        vm_autotest_note("mock_account_db_mysql_save_failed reason=%s error=%s\n",
                         reason ? reason : "state", mysql_error);
        fclose(fp);
        return false;
    }
    for (u32 i = 0; i < header.accountCount; ++i)
    {
        vm_mock_service_account_record record;
        char username_hex[sizeof(record.username) * 2 + 1];
        char password_hex[sizeof(record.password) * 2 + 1];
        char query[768];
        size_t username_len;
        size_t password_len;

        memset(&record, 0, sizeof(record));
        if (fread(&record, 1, sizeof(record), fp) != sizeof(record))
        {
            vm_mysql_exec("ROLLBACK");
            fclose(fp);
            vm_autotest_note("mock_account_db_mysql_save_failed reason=%s error=short-legacy-record index=%u\n",
                             reason ? reason : "state", i);
            return false;
        }
        username_len = vm_mock_mysql_bounded_strlen(record.username, sizeof(record.username));
        password_len = vm_mock_mysql_bounded_strlen(record.password, sizeof(record.password));
        if (username_len == 0 || username_len >= sizeof(record.username) ||
            password_len >= sizeof(record.password) ||
            vm_mysql_hex_encode(record.username, username_len, username_hex, sizeof(username_hex)) == 0 ||
            vm_mysql_hex_encode(record.password, password_len, password_hex, sizeof(password_hex)) == 0)
        {
            vm_mysql_exec("ROLLBACK");
            fclose(fp);
            vm_autotest_note("mock_account_db_mysql_save_failed reason=%s error=invalid-account-record index=%u\n",
                             reason ? reason : "state", i);
            return false;
        }
        snprintf(query, sizeof(query),
                 "INSERT INTO accounts(account_id,password_value) "
                 "VALUES(CAST(X'%s' AS CHAR),X'%s') "
                 "ON DUPLICATE KEY UPDATE password_value=VALUES(password_value)",
                 username_hex, password_hex);
        if (!vm_mysql_exec(query))
        {
            vm_autotest_note("mock_account_db_mysql_save_failed reason=%s index=%u error=%s\n",
                             reason ? reason : "state", i, vm_mysql_last_error());
            vm_mysql_exec("ROLLBACK");
            fclose(fp);
            return false;
        }
        ++imported;
    }
    fclose(fp);
    if (!vm_mysql_exec("COMMIT"))
    {
        vm_autotest_note("mock_account_db_mysql_save_failed reason=%s error=%s\n",
                         reason ? reason : "state", vm_mysql_last_error());
        vm_mysql_exec("ROLLBACK");
        return false;
    }
    if (importedCountOut)
        *importedCountOut = imported;
    vm_autotest_note("mock_account_db_mysql_save reason=%s count=%u\n",
                     reason ? reason : "state", imported);
    return true;
}

static void vm_mock_service_account_db_load(void)
{
    u32 account_count = 0;

    if (g_vm_mock_service_account_db_loaded)
        return;
    g_vm_mock_service_account_db_loaded = true;
    g_vm_mock_service_account_db_valid = true;

    if (!vm_mock_service_account_count(&account_count))
    {
        g_vm_mock_service_account_db_valid = false;
        vm_autotest_note("mock_account_db_mysql_load_failed error=%s\n", vm_mysql_last_error());
        return;
    }
    if (account_count == 0)
    {
        bool has_role_data = false;
        u32 imported = 0;

        /* An empty account table alongside role rows/payload backups is an
         * integrity failure, not a fresh installation.  Importing the old
         * file at this point was the path that made a later process restart
         * appear to restore an account list from the distant past. */
        if (!vm_mock_service_mysql_has_role_data(&has_role_data))
        {
            g_vm_mock_service_account_db_valid = false;
            vm_autotest_note("mock_account_db_mysql_load_failed error=authority-probe:%s\n",
                             vm_mysql_last_error());
            return;
        }
        if (has_role_data)
        {
            g_vm_mock_service_account_db_valid = false;
            vm_autotest_note("mock_account_db_mysql_load_failed error=empty-accounts-with-role-data\n");
            return;
        }
        if (!vm_mock_service_mysql_authority_is_sealed() &&
            vm_mock_service_account_db_migrate_legacy("legacy-migrate", &imported))
        {
            vm_autotest_note("mock_account_db_legacy_migrate count=%u\n", imported);
            account_count = imported;
        }
    }
    vm_autotest_note("mock_account_db_mysql_load count=%u\n", account_count);
}

static void vm_mock_service_friend_db_path(char *path, size_t pathSize)
{
    if (path == NULL || pathSize == 0)
        return;
    snprintf(path, pathSize, "nvram/mock_service_friends.bin");
}

/* Full friendship snapshots are only used for legacy import or startup
 * normalization.  Accepted invitations use the two-row transactional path. */
static bool vm_mock_service_friend_db_save_all(const char *reason)
{
    if (!g_vm_mock_service_friend_db_valid)
        return false;
    memcpy(g_vm_mock_service_friend_db.magic, "JHF1", 4);
    g_vm_mock_service_friend_db.version = VM_MOCK_SERVICE_FRIEND_DB_VERSION;
    if (g_vm_mock_service_friend_db.recordCount > VM_MOCK_SERVICE_FRIEND_DB_MAX_RECORDS)
        g_vm_mock_service_friend_db.recordCount = VM_MOCK_SERVICE_FRIEND_DB_MAX_RECORDS;

    if (!vm_mysql_exec("START TRANSACTION") || !vm_mysql_exec("DELETE FROM friendships"))
    {
        char mysql_error[512];
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        vm_mysql_exec("ROLLBACK");
        vm_autotest_note("mock_friend_db_mysql_save_failed reason=%s error=%s\n",
                         reason ? reason : "state", mysql_error);
        return false;
    }
    for (u32 i = 0; i < g_vm_mock_service_friend_db.recordCount; ++i)
    {
        const vm_mock_service_friend_record *record = &g_vm_mock_service_friend_db.records[i];
        char owner_hex[sizeof(record->ownerAccountId) * 2 + 1];
        char target_hex[sizeof(record->targetAccountId) * 2 + 1];
        char name_hex[sizeof(record->targetRoleName) * 2 + 1];
        char query[1536];
        size_t owner_len = vm_mock_mysql_bounded_strlen(record->ownerAccountId, sizeof(record->ownerAccountId));
        size_t target_len = vm_mock_mysql_bounded_strlen(record->targetAccountId, sizeof(record->targetAccountId));
        size_t name_len = vm_mock_mysql_bounded_strlen(record->targetRoleName, sizeof(record->targetRoleName));
        if (owner_len == 0 || owner_len >= sizeof(record->ownerAccountId) ||
            target_len == 0 || target_len >= sizeof(record->targetAccountId) ||
            name_len >= sizeof(record->targetRoleName) ||
            vm_mysql_hex_encode(record->ownerAccountId, owner_len, owner_hex, sizeof(owner_hex)) == 0 ||
            vm_mysql_hex_encode(record->targetAccountId, target_len, target_hex, sizeof(target_hex)) == 0 ||
            (name_len != 0 && vm_mysql_hex_encode(record->targetRoleName, name_len, name_hex, sizeof(name_hex)) == 0))
        {
            vm_mysql_exec("ROLLBACK");
            vm_autotest_note("mock_friend_db_mysql_save_failed reason=%s error=invalid-friend-record index=%u\n",
                             reason ? reason : "state", i);
            return false;
        }
        if (name_len == 0)
            name_hex[0] = 0;
        snprintf(query, sizeof(query),
                 "INSERT INTO friendships(owner_account_id,owner_role_id,target_account_id,target_role_id,target_role_name,friend_degree,target_level,target_job,target_sex) "
                 "VALUES(CAST(X'%s' AS CHAR),%u,CAST(X'%s' AS CHAR),%u,X'%s',%u,%u,%u,%u)",
                 owner_hex, record->ownerRoleId, target_hex, record->targetRoleId, name_hex,
                 record->friendDegree, record->targetLevel, record->targetJob, record->targetSex);
        if (!vm_mysql_exec(query))
        {
            vm_autotest_note("mock_friend_db_mysql_save_failed reason=%s index=%u error=%s\n",
                             reason ? reason : "state", i, vm_mysql_last_error());
            vm_mysql_exec("ROLLBACK");
            return false;
        }
    }
    if (!vm_mysql_exec("COMMIT"))
    {
        vm_autotest_note("mock_friend_db_mysql_save_failed reason=%s error=%s\n",
                         reason ? reason : "state", vm_mysql_last_error());
        vm_mysql_exec("ROLLBACK");
        return false;
    }
    vm_autotest_note("mock_friend_db_mysql_save reason=%s records=%u\n",
                     reason ? reason : "state",
                     g_vm_mock_service_friend_db.recordCount);
    return true;
}

static bool vm_mock_service_friend_db_write_pair(
    const vm_mock_service_friend_record *forward,
    const vm_mock_service_friend_record *reverse,
    const char *reason)
{
    const vm_mock_service_friend_record *records[2] = { forward, reverse };
    char queries[2][1536];
    char mysql_error[512];
    bool transaction_started = false;

    /* Friend reads are on-demand MySQL after seal; write_pair only needs the
     * friendships table to be reachable, not the legacy in-memory cache. */
    if (forward == NULL || reverse == NULL)
        return false;
    if (!g_vm_mock_service_friend_db_valid && !vm_mock_service_mysql_authority_is_sealed())
        return false;
    for (u32 i = 0; i < 2; ++i)
    {
        const vm_mock_service_friend_record *record = records[i];
        char owner_hex[sizeof(record->ownerAccountId) * 2 + 1];
        char target_hex[sizeof(record->targetAccountId) * 2 + 1];
        char name_hex[sizeof(record->targetRoleName) * 2 + 1];
        size_t owner_len = vm_mock_mysql_bounded_strlen(record->ownerAccountId, sizeof(record->ownerAccountId));
        size_t target_len = vm_mock_mysql_bounded_strlen(record->targetAccountId, sizeof(record->targetAccountId));
        size_t name_len = vm_mock_mysql_bounded_strlen(record->targetRoleName, sizeof(record->targetRoleName));

        if (owner_len == 0 || owner_len >= sizeof(record->ownerAccountId) ||
            target_len == 0 || target_len >= sizeof(record->targetAccountId) ||
            name_len >= sizeof(record->targetRoleName) ||
            vm_mysql_hex_encode(record->ownerAccountId, owner_len, owner_hex, sizeof(owner_hex)) == 0 ||
            vm_mysql_hex_encode(record->targetAccountId, target_len, target_hex, sizeof(target_hex)) == 0 ||
            (name_len != 0 && vm_mysql_hex_encode(record->targetRoleName, name_len, name_hex, sizeof(name_hex)) == 0))
        {
            vm_autotest_note("mock_friend_db_mysql_pair_failed reason=%s error=invalid-friend-record index=%u\n",
                             reason ? reason : "state", i);
            return false;
        }
        if (name_len == 0)
            name_hex[0] = 0;
        snprintf(queries[i], sizeof(queries[i]),
                 "INSERT INTO friendships(owner_account_id,owner_role_id,target_account_id,target_role_id,target_role_name,friend_degree,target_level,target_job,target_sex) "
                 "VALUES(CAST(X'%s' AS CHAR),%u,CAST(X'%s' AS CHAR),%u,X'%s',%u,%u,%u,%u) "
                 "ON DUPLICATE KEY UPDATE target_role_name=VALUES(target_role_name),friend_degree=VALUES(friend_degree),target_level=VALUES(target_level),target_job=VALUES(target_job),target_sex=VALUES(target_sex)",
                 owner_hex, record->ownerRoleId, target_hex, record->targetRoleId, name_hex,
                 record->friendDegree, record->targetLevel, record->targetJob, record->targetSex);
    }
    if (!vm_mysql_exec("START TRANSACTION"))
        goto failed;
    transaction_started = true;
    if (!vm_mysql_exec(queries[0]) || !vm_mysql_exec(queries[1]) ||
        !vm_mysql_exec("COMMIT"))
    {
        goto failed;
    }
    vm_autotest_note("mock_friend_db_mysql_pair reason=%s records=2\n",
                     reason ? reason : "state");
    return true;

failed:
    snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
    if (transaction_started)
        vm_mysql_exec("ROLLBACK");
    vm_autotest_note("mock_friend_db_mysql_pair_failed reason=%s error=%s\n",
                     reason ? reason : "state", mysql_error);
    return false;
}

typedef struct
{
    vm_mock_service_friend_db_file *database;
    bool invalid;
} vm_mock_mysql_friend_load_context;

static bool vm_mock_mysql_friend_row(void *context_value,
                                     unsigned int column_count,
                                     const char *const *values,
                                     const size_t *lengths)
{
    vm_mock_mysql_friend_load_context *context = (vm_mock_mysql_friend_load_context *)context_value;
    if (context == NULL || context->database == NULL || column_count != 9 ||
        context->database->recordCount >= VM_MOCK_SERVICE_FRIEND_DB_MAX_RECORDS)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    vm_mock_service_friend_record *record =
        &context->database->records[context->database->recordCount];
    size_t name_len = 0;
    u32 target_job = 0;
    u32 target_sex = 0;
    memset(record, 0, sizeof(*record));
    if (!vm_mock_mysql_copy_text(record->ownerAccountId, sizeof(record->ownerAccountId), values[0], lengths[0]) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &record->ownerRoleId) ||
        !vm_mock_mysql_copy_text(record->targetAccountId, sizeof(record->targetAccountId), values[2], lengths[2]) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &record->targetRoleId) ||
        values[4] == NULL ||
        !vm_mysql_hex_decode(values[4], lengths[4], record->targetRoleName,
                             sizeof(record->targetRoleName) - 1, &name_len) ||
        !vm_mock_mysql_parse_u32(values[5], lengths[5], &record->friendDegree) ||
        !vm_mock_mysql_parse_u32(values[6], lengths[6], &record->targetLevel) ||
        !vm_mock_mysql_parse_u32(values[7], lengths[7], &target_job) || target_job > 255 ||
        !vm_mock_mysql_parse_u32(values[8], lengths[8], &target_sex) || target_sex > 255)
    {
        context->invalid = true;
        memset(record, 0, sizeof(*record));
        return true;
    }
    record->targetRoleName[name_len] = 0;
    record->targetJob = (u8)target_job;
    record->targetSex = (u8)target_sex;
    ++context->database->recordCount;
    return true;
}

static void vm_mock_service_friend_db_load(void)
{
    char path[128];
    vm_mock_service_friend_db_file loaded;
    vm_mock_service_friend_record compact[VM_MOCK_SERVICE_FRIEND_DB_MAX_RECORDS];
    u32 compactCount = 0;
    bool needsSave = false;
    bool loadedFromFile = false;

    if (g_vm_mock_service_friend_db_loaded)
        return;
    g_vm_mock_service_friend_db_loaded = true;
    memset(&g_vm_mock_service_friend_db, 0, sizeof(g_vm_mock_service_friend_db));
    memcpy(g_vm_mock_service_friend_db.magic, "JHF1", 4);
    g_vm_mock_service_friend_db.version = VM_MOCK_SERVICE_FRIEND_DB_VERSION;
    g_vm_mock_service_friend_db_valid = true;

    /* After seal, friendships are read per request from MySQL.  Keep only the
     * one-shot legacy/normalize import path for unsealed databases. */
    if (vm_mock_service_mysql_authority_is_sealed())
    {
        printf("[info][mock-service] friend_db_cache skipped reason=already-sealed mode=on-demand-mysql\n");
        vm_autotest_note("mock_friend_db_on_demand\n");
        return;
    }

    memset(&loaded, 0, sizeof(loaded));
    memcpy(loaded.magic, "JHF1", 4);
    loaded.version = VM_MOCK_SERVICE_FRIEND_DB_VERSION;
    vm_mock_mysql_friend_load_context context;
    memset(&context, 0, sizeof(context));
    context.database = &loaded;
    if (!vm_mysql_query(
            "SELECT owner_account_id,owner_role_id,target_account_id,target_role_id,HEX(target_role_name),friend_degree,target_level,target_job,target_sex "
            "FROM friendships ORDER BY owner_account_id,owner_role_id,target_account_id,target_role_id",
            vm_mock_mysql_friend_row, &context))
    {
        g_vm_mock_service_friend_db_valid = false;
        vm_autotest_note("mock_friend_db_mysql_load_failed error=%s\n", vm_mysql_last_error());
        return;
    }
    if (context.invalid)
    {
        g_vm_mock_service_friend_db_valid = false;
        vm_autotest_note("mock_friend_db_mysql_load_failed error=invalid-row\n");
        return;
    }
    if (loaded.recordCount == 0)
    {
        vm_mock_service_friend_db_path(path, sizeof(path));
        FILE *fp = fopen(path, "rb");
        if (fp != NULL)
        {
            vm_mock_service_friend_db_file legacy;
            memset(&legacy, 0, sizeof(legacy));
            if (fread(&legacy, 1, sizeof(legacy), fp) == sizeof(legacy) &&
                memcmp(legacy.magic, "JHF1", 4) == 0 &&
                legacy.version == VM_MOCK_SERVICE_FRIEND_DB_VERSION &&
                legacy.recordCount <= VM_MOCK_SERVICE_FRIEND_DB_MAX_RECORDS)
            {
                loaded = legacy;
                loadedFromFile = true;
                needsSave = true;
                vm_autotest_note("mock_friend_db_legacy_migrate records=%u\n", loaded.recordCount);
            }
            fclose(fp);
        }
    }

    memset(compact, 0, sizeof(compact));
    for (u32 i = 0; i < loaded.recordCount; ++i)
    {
        vm_mock_service_friend_record record = loaded.records[i];
        bool duplicate = false;

        record.ownerAccountId[sizeof(record.ownerAccountId) - 1] = 0;
        record.targetAccountId[sizeof(record.targetAccountId) - 1] = 0;
        record.targetRoleName[sizeof(record.targetRoleName) - 1] = 0;
        if (record.ownerAccountId[0] == 0 || record.targetAccountId[0] == 0 ||
            record.ownerRoleId == 0 || record.targetRoleId == 0 ||
            (record.ownerRoleId == record.targetRoleId &&
             strcmp(record.ownerAccountId, record.targetAccountId) == 0))
        {
            needsSave = true;
            continue;
        }
        for (u32 j = 0; j < compactCount; ++j)
        {
            if (compact[j].ownerRoleId == record.ownerRoleId &&
                compact[j].targetRoleId == record.targetRoleId &&
                strcmp(compact[j].ownerAccountId, record.ownerAccountId) == 0 &&
                strcmp(compact[j].targetAccountId, record.targetAccountId) == 0)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
        {
            needsSave = true;
            continue;
        }
        if (record.targetRoleName[0] == 0)
            snprintf(record.targetRoleName, sizeof(record.targetRoleName), "Player");
        if (record.targetLevel == 0)
            record.targetLevel = 1;
        if (record.targetJob == 0 || record.targetJob > 3)
            record.targetJob = 1;
        compact[compactCount++] = record;
    }
    memset(&g_vm_mock_service_friend_db, 0, sizeof(g_vm_mock_service_friend_db));
    memcpy(g_vm_mock_service_friend_db.magic, "JHF1", 4);
    g_vm_mock_service_friend_db.version = VM_MOCK_SERVICE_FRIEND_DB_VERSION;
    g_vm_mock_service_friend_db.recordCount = compactCount;
    if (compactCount > 0)
        memcpy(g_vm_mock_service_friend_db.records, compact,
               compactCount * sizeof(compact[0]));
    if (needsSave &&
        !vm_mock_service_friend_db_save_all(loadedFromFile ? "legacy-migrate" : "normalize"))
    {
        g_vm_mock_service_friend_db_valid = false;
        return;
    }
    vm_autotest_note("mock_friend_db_mysql_load records=%u\n",
                     g_vm_mock_service_friend_db.recordCount);
}

typedef struct
{
    bool found;
    bool invalid;
} vm_mock_mysql_friend_exists_context;

typedef struct
{
    vm_mock_service_friend_record *records;
    u32 capacity;
    u32 count;
    bool invalid;
} vm_mock_mysql_friend_list_context;

typedef struct
{
    vm_mock_service_friend_record *record;
    bool found;
    bool invalid;
} vm_mock_mysql_friend_one_context;

static bool vm_mock_mysql_friend_exists_row(void *context_value,
                                           unsigned int column_count,
                                           const char *const *values,
                                           const size_t *lengths)
{
    vm_mock_mysql_friend_exists_context *context =
        (vm_mock_mysql_friend_exists_context *)context_value;
    (void)values;
    (void)lengths;
    if (context == NULL || column_count < 1)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_mock_mysql_friend_list_row(void *context_value,
                                         unsigned int column_count,
                                         const char *const *values,
                                         const size_t *lengths)
{
    vm_mock_mysql_friend_list_context *context =
        (vm_mock_mysql_friend_list_context *)context_value;
    vm_mock_service_friend_record *record = NULL;
    size_t name_len = 0;
    u32 target_job = 0;
    u32 target_sex = 0;

    if (context == NULL || context->records == NULL || column_count != 9)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    if (context->count >= context->capacity)
        return true;
    record = &context->records[context->count];
    memset(record, 0, sizeof(*record));
    if (!vm_mock_mysql_copy_text(record->ownerAccountId, sizeof(record->ownerAccountId), values[0], lengths[0]) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &record->ownerRoleId) ||
        !vm_mock_mysql_copy_text(record->targetAccountId, sizeof(record->targetAccountId), values[2], lengths[2]) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &record->targetRoleId) ||
        values[4] == NULL ||
        !vm_mysql_hex_decode(values[4], lengths[4], record->targetRoleName,
                             sizeof(record->targetRoleName) - 1, &name_len) ||
        !vm_mock_mysql_parse_u32(values[5], lengths[5], &record->friendDegree) ||
        !vm_mock_mysql_parse_u32(values[6], lengths[6], &record->targetLevel) ||
        !vm_mock_mysql_parse_u32(values[7], lengths[7], &target_job) || target_job > 255 ||
        !vm_mock_mysql_parse_u32(values[8], lengths[8], &target_sex) || target_sex > 255)
    {
        context->invalid = true;
        memset(record, 0, sizeof(*record));
        return true;
    }
    record->targetRoleName[name_len] = 0;
    record->targetJob = (u8)target_job;
    record->targetSex = (u8)target_sex;
    if (record->targetRoleName[0] == 0)
        snprintf(record->targetRoleName, sizeof(record->targetRoleName), "Player");
    if (record->targetLevel == 0)
        record->targetLevel = 1;
    if (record->targetJob == 0 || record->targetJob > 3)
        record->targetJob = 1;
    ++context->count;
    return true;
}

static bool vm_mock_mysql_friend_one_row(void *context_value,
                                        unsigned int column_count,
                                        const char *const *values,
                                        const size_t *lengths)
{
    vm_mock_mysql_friend_one_context *context =
        (vm_mock_mysql_friend_one_context *)context_value;
    vm_mock_mysql_friend_list_context list;

    if (context == NULL || context->record == NULL)
        return true;
    memset(&list, 0, sizeof(list));
    list.records = context->record;
    list.capacity = 1;
    if (!vm_mock_mysql_friend_list_row(&list, column_count, values, lengths) ||
        list.invalid)
    {
        context->invalid = true;
        return true;
    }
    if (list.count > 0)
        context->found = true;
    return true;
}

static bool vm_mock_service_friend_db_mysql_exists(
    const char *ownerAccountId, u32 ownerRoleId,
    const char *targetAccountId, u32 targetRoleId,
    bool *existsOut)
{
    char owner_hex[129];
    char target_hex[129];
    char query[512];
    size_t owner_len;
    size_t target_len;
    vm_mock_mysql_friend_exists_context context;

    if (existsOut)
        *existsOut = false;
    if (ownerAccountId == NULL || ownerAccountId[0] == 0 || ownerRoleId == 0 ||
        targetAccountId == NULL || targetAccountId[0] == 0 || targetRoleId == 0)
    {
        return false;
    }
    owner_len = vm_mock_mysql_bounded_strlen(ownerAccountId, 64);
    target_len = vm_mock_mysql_bounded_strlen(targetAccountId, 64);
    if (owner_len == 0 || owner_len >= 64 || target_len == 0 || target_len >= 64 ||
        vm_mysql_hex_encode(ownerAccountId, owner_len, owner_hex, sizeof(owner_hex)) == 0 ||
        vm_mysql_hex_encode(targetAccountId, target_len, target_hex, sizeof(target_hex)) == 0)
    {
        return false;
    }
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT 1 FROM friendships WHERE owner_account_id=CAST(X'%s' AS CHAR) AND "
             "owner_role_id=%u AND target_account_id=CAST(X'%s' AS CHAR) AND "
             "target_role_id=%u LIMIT 1",
             owner_hex, ownerRoleId, target_hex, targetRoleId);
    if (!vm_mysql_query(query, vm_mock_mysql_friend_exists_row, &context) ||
        context.invalid)
    {
        return false;
    }
    if (existsOut)
        *existsOut = context.found;
    return true;
}

static bool vm_mock_service_friend_db_mysql_count_owner(
    const char *ownerAccountId, u32 ownerRoleId, u32 *countOut)
{
    char owner_hex[129];
    char query[384];
    size_t owner_len;
    vm_mock_mysql_authority_count_context context;

    if (countOut)
        *countOut = 0;
    if (ownerAccountId == NULL || ownerAccountId[0] == 0 || ownerRoleId == 0)
        return false;
    owner_len = vm_mock_mysql_bounded_strlen(ownerAccountId, 64);
    if (owner_len == 0 || owner_len >= 64 ||
        vm_mysql_hex_encode(ownerAccountId, owner_len, owner_hex, sizeof(owner_hex)) == 0)
    {
        return false;
    }
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM friendships WHERE owner_account_id=CAST(X'%s' AS CHAR) "
             "AND owner_role_id=%u",
             owner_hex, ownerRoleId);
    if (!vm_mysql_query(query, vm_mock_mysql_authority_count_row, &context) ||
        context.invalid || !context.found)
    {
        return false;
    }
    if (countOut)
        *countOut = context.value;
    return true;
}

static bool vm_mock_service_friend_db_mysql_list_owner_page(
    const char *ownerAccountId, u32 ownerRoleId,
    u32 skip, u32 limit,
    vm_mock_service_friend_record *recordsOut, u32 recordsCap,
    u32 *returnedOut)
{
    char owner_hex[129];
    char query[512];
    size_t owner_len;
    vm_mock_mysql_friend_list_context context;

    if (returnedOut)
        *returnedOut = 0;
    if (ownerAccountId == NULL || ownerAccountId[0] == 0 || ownerRoleId == 0 ||
        recordsOut == NULL || recordsCap == 0 || limit == 0)
    {
        return false;
    }
    if (limit > recordsCap)
        limit = recordsCap;
    owner_len = vm_mock_mysql_bounded_strlen(ownerAccountId, 64);
    if (owner_len == 0 || owner_len >= 64 ||
        vm_mysql_hex_encode(ownerAccountId, owner_len, owner_hex, sizeof(owner_hex)) == 0)
    {
        return false;
    }
    memset(&context, 0, sizeof(context));
    context.records = recordsOut;
    context.capacity = limit;
    snprintf(query, sizeof(query),
             "SELECT owner_account_id,owner_role_id,target_account_id,target_role_id,"
             "HEX(target_role_name),friend_degree,target_level,target_job,target_sex "
             "FROM friendships WHERE owner_account_id=CAST(X'%s' AS CHAR) AND "
             "owner_role_id=%u ORDER BY target_role_id LIMIT %u OFFSET %u",
             owner_hex, ownerRoleId, limit, skip);
    if (!vm_mysql_query(query, vm_mock_mysql_friend_list_row, &context) ||
        context.invalid)
    {
        return false;
    }
    if (returnedOut)
        *returnedOut = context.count;
    return true;
}

static bool vm_net_mock_role_ensure_intimacy_marriage_columns(void);

static void vm_mock_service_friend_intimacy_bump_pair(
    const char *accountA, u32 roleA,
    const char *accountB, u32 roleB)
{
    char hexA[129];
    char hexB[129];
    char query[768];
    size_t lenA;
    size_t lenB;

    if (accountA == NULL || accountB == NULL || roleA == 0 || roleB == 0 ||
        roleA == roleB)
        return;
    lenA = vm_mock_mysql_bounded_strlen(accountA, 64);
    lenB = vm_mock_mysql_bounded_strlen(accountB, 64);
    if (lenA == 0 || lenB == 0 || lenA >= 64 || lenB >= 64 ||
        vm_mysql_hex_encode(accountA, lenA, hexA, sizeof(hexA)) == 0 ||
        vm_mysql_hex_encode(accountB, lenB, hexB, sizeof(hexB)) == 0)
        return;
    snprintf(query, sizeof(query),
             "UPDATE friendships SET friend_degree=friend_degree+1,"
             "last_team_at=CURRENT_TIMESTAMP "
             "WHERE owner_account_id=CAST(X'%s' AS CHAR) AND owner_role_id=%u "
             "AND target_account_id=CAST(X'%s' AS CHAR) AND target_role_id=%u",
             hexA, roleA, hexB, roleB);
    if (!vm_mysql_exec(query))
    {
        printf("[warn][mock-service] intimacy_bump failed a=%s/%u b=%s/%u err=%s\n",
               accountA, roleA, accountB, roleB, vm_mysql_last_error());
        return;
    }
    snprintf(query, sizeof(query),
             "UPDATE friendships SET friend_degree=friend_degree+1,"
             "last_team_at=CURRENT_TIMESTAMP "
             "WHERE owner_account_id=CAST(X'%s' AS CHAR) AND owner_role_id=%u "
             "AND target_account_id=CAST(X'%s' AS CHAR) AND target_role_id=%u",
             hexB, roleB, hexA, roleA);
    if (!vm_mysql_exec(query))
    {
        printf("[warn][mock-service] intimacy_bump reverse failed a=%s/%u b=%s/%u "
               "err=%s\n",
               accountB, roleB, accountA, roleA, vm_mysql_last_error());
    }
}

/* Decay: if no team for >=3 days and last decay also >=3 days (or never), -10. */
static void vm_mock_service_friend_intimacy_decay_owner(const char *ownerAccountId,
                                                        u32 ownerRoleId)
{
    char owner_hex[129];
    char query[1024];
    size_t owner_len;

    if (ownerAccountId == NULL || ownerAccountId[0] == 0 || ownerRoleId == 0)
        return;
    if (!vm_net_mock_role_ensure_intimacy_marriage_columns())
        return;
    owner_len = vm_mock_mysql_bounded_strlen(ownerAccountId, 64);
    if (owner_len == 0 || owner_len >= 64 ||
        vm_mysql_hex_encode(ownerAccountId, owner_len, owner_hex, sizeof(owner_hex)) == 0)
        return;
    snprintf(query, sizeof(query),
             "UPDATE friendships SET "
             "friend_degree=IF(friend_degree>=10,friend_degree-10,0),"
             "last_decay_at=CURRENT_TIMESTAMP "
             "WHERE owner_account_id=CAST(X'%s' AS CHAR) AND owner_role_id=%u "
             "AND last_team_at <= (NOW() - INTERVAL 3 DAY) "
             "AND (last_decay_at IS NULL OR last_decay_at <= (NOW() - INTERVAL 3 DAY))",
             owner_hex, ownerRoleId);
    if (!vm_mysql_exec(query))
    {
        printf("[warn][mock-service] intimacy_decay failed owner=%s/%u err=%s\n",
               ownerAccountId, ownerRoleId, vm_mysql_last_error());
        return;
    }
    printf("[info][mock-service] intimacy_decay owner=%s/%u evidence=3day-no-team\n",
           ownerAccountId, ownerRoleId);
}

static bool vm_mock_service_marriage_persist_pair(
    const char *accountA, u32 roleA, const char *nameA,
    const char *accountB, u32 roleB, const char *nameB,
    char *errOut, size_t errCap)
{
    char hexA[129];
    char hexB[129];
    char nameAHex[65];
    char nameBHex[65];
    char query[1024];
    size_t lenA;
    size_t lenB;
    size_t nA;
    size_t nB;

    if (errOut && errCap)
        errOut[0] = 0;
    if (accountA == NULL || accountB == NULL || roleA == 0 || roleB == 0 ||
        nameA == NULL || nameB == NULL)
    {
        if (errOut && errCap)
            snprintf(errOut, errCap, "bad-args");
        return false;
    }
    lenA = vm_mock_mysql_bounded_strlen(accountA, 64);
    lenB = vm_mock_mysql_bounded_strlen(accountB, 64);
    nA = vm_mock_mysql_bounded_strlen(nameA, 32);
    nB = vm_mock_mysql_bounded_strlen(nameB, 32);
    if (lenA == 0 || lenB == 0 ||
        vm_mysql_hex_encode(accountA, lenA, hexA, sizeof(hexA)) == 0 ||
        vm_mysql_hex_encode(accountB, lenB, hexB, sizeof(hexB)) == 0 ||
        (nA && vm_mysql_hex_encode(nameA, nA, nameAHex, sizeof(nameAHex)) == 0) ||
        (nB && vm_mysql_hex_encode(nameB, nB, nameBHex, sizeof(nameBHex)) == 0))
    {
        if (errOut && errCap)
            snprintf(errOut, errCap, "hex");
        return false;
    }
    if (!nA)
        nameAHex[0] = 0;
    if (!nB)
        nameBHex[0] = 0;
    if (!vm_net_mock_role_ensure_intimacy_marriage_columns())
    {
        if (errOut && errCap)
            snprintf(errOut, errCap, "schema");
        return false;
    }
    {
        vm_mock_mysql_authority_count_context cnt;
        memset(&cnt, 0, sizeof(cnt));
        snprintf(query, sizeof(query),
                 "SELECT COUNT(*) FROM account_roles WHERE "
                 "((account_id=CAST(X'%s' AS CHAR) AND role_id=%u) OR "
                 "(account_id=CAST(X'%s' AS CHAR) AND role_id=%u)) "
                 "AND spouse_role_id<>0",
                 hexA, roleA, hexB, roleB);
        if (!vm_mysql_query(query, vm_mock_mysql_authority_count_row, &cnt) ||
            cnt.invalid || !cnt.found)
        {
            if (errOut && errCap)
                snprintf(errOut, errCap, "query");
            return false;
        }
        if (cnt.value > 0)
        {
            if (errOut && errCap)
                snprintf(errOut, errCap, "already-married");
            return false;
        }
    }
    snprintf(query, sizeof(query),
             "UPDATE account_roles SET spouse_role_id=%u,"
             "spouse_account_id=CAST(X'%s' AS CHAR),spouse_name=X'%s' "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
             roleB, hexB, nameBHex, hexA, roleA);
    if (!vm_mysql_exec(query))
    {
        if (errOut && errCap)
            snprintf(errOut, errCap, "%s", vm_mysql_last_error());
        return false;
    }
    snprintf(query, sizeof(query),
             "UPDATE account_roles SET spouse_role_id=%u,"
             "spouse_account_id=CAST(X'%s' AS CHAR),spouse_name=X'%s' "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
             roleA, hexA, nameAHex, hexB, roleB);
    if (!vm_mysql_exec(query))
    {
        if (errOut && errCap)
            snprintf(errOut, errCap, "%s", vm_mysql_last_error());
        return false;
    }
    return true;
}

static bool vm_mock_service_friend_db_mysql_find_owner_target(
    const char *ownerAccountId, u32 ownerRoleId, u32 targetRoleId,
    vm_mock_service_friend_record *recordOut)
{
    char owner_hex[129];
    char query[512];
    size_t owner_len;
    vm_mock_mysql_friend_one_context context;

    if (recordOut)
        memset(recordOut, 0, sizeof(*recordOut));
    if (ownerAccountId == NULL || ownerAccountId[0] == 0 || ownerRoleId == 0 ||
        targetRoleId == 0 || recordOut == NULL)
    {
        return false;
    }
    owner_len = vm_mock_mysql_bounded_strlen(ownerAccountId, 64);
    if (owner_len == 0 || owner_len >= 64 ||
        vm_mysql_hex_encode(ownerAccountId, owner_len, owner_hex, sizeof(owner_hex)) == 0)
    {
        return false;
    }
    memset(&context, 0, sizeof(context));
    context.record = recordOut;
    snprintf(query, sizeof(query),
             "SELECT owner_account_id,owner_role_id,target_account_id,target_role_id,"
             "HEX(target_role_name),friend_degree,target_level,target_job,target_sex "
             "FROM friendships WHERE owner_account_id=CAST(X'%s' AS CHAR) AND "
             "owner_role_id=%u AND target_role_id=%u LIMIT 1",
             owner_hex, ownerRoleId, targetRoleId);
    if (!vm_mysql_query(query, vm_mock_mysql_friend_one_row, &context) ||
        context.invalid)
    {
        return false;
    }
    return context.found;
}

static vm_mock_service_friend_record *vm_mock_service_friend_db_find_in(
    vm_mock_service_friend_db_file *database,
    const char *ownerAccountId, u32 ownerRoleId,
    const char *targetAccountId, u32 targetRoleId)
{
    if (database == NULL || ownerAccountId == NULL ||
        targetAccountId == NULL || ownerRoleId == 0 || targetRoleId == 0)
    {
        return NULL;
    }
    for (u32 i = 0; i < database->recordCount; ++i)
    {
        vm_mock_service_friend_record *record = &database->records[i];
        if (record->ownerRoleId == ownerRoleId &&
            record->targetRoleId == targetRoleId &&
            strcmp(record->ownerAccountId, ownerAccountId) == 0 &&
            strcmp(record->targetAccountId, targetAccountId) == 0)
        {
            return record;
        }
    }
    return NULL;
}

static vm_mock_service_friend_record *vm_mock_service_friend_db_find(
    const char *ownerAccountId, u32 ownerRoleId,
    const char *targetAccountId, u32 targetRoleId)
{
    vm_mock_service_friend_db_load();
    if (!g_vm_mock_service_friend_db_valid)
        return NULL;
    return vm_mock_service_friend_db_find_in(&g_vm_mock_service_friend_db,
                                             ownerAccountId, ownerRoleId,
                                             targetAccountId, targetRoleId);
}

static bool vm_mock_service_friend_db_upsert_one(
    vm_mock_service_friend_db_file *database,
    const char *ownerAccountId, u32 ownerRoleId,
    const char *targetAccountId, u32 targetRoleId,
    const char *targetRoleName, u32 targetLevel, u8 targetJob, u8 targetSex,
    bool *createdOut, bool *changedOut)
{
    vm_mock_service_friend_record *record = NULL;
    bool created = false;
    bool changed = false;

    if (createdOut)
        *createdOut = false;
    if (changedOut)
        *changedOut = false;
    if (database == NULL)
        return false;
    record = vm_mock_service_friend_db_find_in(database, ownerAccountId, ownerRoleId,
                                               targetAccountId, targetRoleId);
    if (record == NULL)
    {
        if (database->recordCount >= VM_MOCK_SERVICE_FRIEND_DB_MAX_RECORDS)
            return false;
        record = &database->records[database->recordCount++];
        memset(record, 0, sizeof(*record));
        snprintf(record->ownerAccountId, sizeof(record->ownerAccountId), "%s", ownerAccountId);
        record->ownerRoleId = ownerRoleId;
        snprintf(record->targetAccountId, sizeof(record->targetAccountId), "%s", targetAccountId);
        record->targetRoleId = targetRoleId;
        created = true;
        changed = true;
    }
    if (targetRoleName != NULL && targetRoleName[0] != 0 &&
        strcmp(record->targetRoleName, targetRoleName) != 0)
    {
        snprintf(record->targetRoleName, sizeof(record->targetRoleName), "%s", targetRoleName);
        changed = true;
    }
    if (record->targetLevel != (targetLevel ? targetLevel : 1))
    {
        record->targetLevel = targetLevel ? targetLevel : 1;
        changed = true;
    }
    if (targetJob == 0 || targetJob > 3)
        targetJob = 1;
    if (record->targetJob != targetJob)
    {
        record->targetJob = targetJob;
        changed = true;
    }
    if (record->targetSex != (targetSex <= 1 ? targetSex : 0))
    {
        record->targetSex = targetSex <= 1 ? targetSex : 0;
        changed = true;
    }
    if (record->targetRoleName[0] == 0)
    {
        snprintf(record->targetRoleName, sizeof(record->targetRoleName), "Player");
        changed = true;
    }
    if (createdOut)
        *createdOut = created;
    if (changedOut)
        *changedOut = changed;
    return true;
}

static void vm_mock_service_friend_db_fill_record(
    vm_mock_service_friend_record *record,
    const char *ownerAccountId, u32 ownerRoleId,
    const char *targetAccountId, u32 targetRoleId,
    const char *targetRoleName, u32 targetLevel, u8 targetJob, u8 targetSex)
{
    if (record == NULL)
        return;
    memset(record, 0, sizeof(*record));
    if (ownerAccountId != NULL)
        snprintf(record->ownerAccountId, sizeof(record->ownerAccountId), "%s", ownerAccountId);
    record->ownerRoleId = ownerRoleId;
    if (targetAccountId != NULL)
        snprintf(record->targetAccountId, sizeof(record->targetAccountId), "%s", targetAccountId);
    record->targetRoleId = targetRoleId;
    if (targetRoleName != NULL && targetRoleName[0] != 0)
        snprintf(record->targetRoleName, sizeof(record->targetRoleName), "%s", targetRoleName);
    else
        snprintf(record->targetRoleName, sizeof(record->targetRoleName), "Player");
    record->targetLevel = targetLevel ? targetLevel : 1;
    record->targetJob = (targetJob == 0 || targetJob > 3) ? 1 : targetJob;
    record->targetSex = targetSex <= 1 ? targetSex : 0;
}

static bool vm_mock_service_friend_db_add_pair(
    const char *ownerAccountId, u32 ownerRoleId,
    const char *ownerRoleName, u32 ownerLevel, u8 ownerJob, u8 ownerSex,
    const char *targetAccountId, u32 targetRoleId,
    const char *targetRoleName, u32 targetLevel, u8 targetJob, u8 targetSex,
    bool *createdOut)
{
    bool forwardExists = false;
    bool reverseExists = false;
    vm_mock_service_friend_record forwardRecord;
    vm_mock_service_friend_record reverseRecord;

    if (createdOut)
        *createdOut = false;
    if (ownerAccountId == NULL || ownerAccountId[0] == 0 || ownerRoleId == 0 ||
        targetAccountId == NULL || targetAccountId[0] == 0 || targetRoleId == 0 ||
        (ownerRoleId == targetRoleId && strcmp(ownerAccountId, targetAccountId) == 0))
    {
        return false;
    }
    vm_mock_service_friend_db_load();
    if (!g_vm_mock_service_friend_db_valid && !vm_mock_service_mysql_authority_is_sealed())
        return false;
    if (!vm_mock_service_friend_db_mysql_exists(ownerAccountId, ownerRoleId,
                                                 targetAccountId, targetRoleId,
                                                 &forwardExists) ||
        !vm_mock_service_friend_db_mysql_exists(targetAccountId, targetRoleId,
                                                 ownerAccountId, ownerRoleId,
                                                 &reverseExists))
    {
        printf("[error][mock-service] friend_pair_lookup_failed owner=%s/%u target=%s/%u error=%s\n",
               ownerAccountId, ownerRoleId, targetAccountId, targetRoleId,
               vm_mysql_last_error());
        return false;
    }
    vm_mock_service_friend_db_fill_record(&forwardRecord,
                                           ownerAccountId, ownerRoleId,
                                           targetAccountId, targetRoleId,
                                           targetRoleName, targetLevel, targetJob, targetSex);
    vm_mock_service_friend_db_fill_record(&reverseRecord,
                                           targetAccountId, targetRoleId,
                                           ownerAccountId, ownerRoleId,
                                           ownerRoleName, ownerLevel, ownerJob, ownerSex);
    if (!vm_mock_service_friend_db_write_pair(&forwardRecord, &reverseRecord,
                                              "friend-invite-accepted"))
    {
        printf("[error][mock-service] friend_pair_persist_failed owner=%s/%u target=%s/%u error=%s\n",
               ownerAccountId, ownerRoleId, targetAccountId, targetRoleId,
               vm_mysql_last_error());
        return false;
    }
    if (createdOut)
        *createdOut = !forwardExists || !reverseExists;
    return true;
}

static bool vm_mock_service_friend_db_mysql_delete_pair(
    const char *ownerAccountId, u32 ownerRoleId,
    const char *targetAccountId, u32 targetRoleId,
    bool *removedOut)
{
    char owner_hex[129];
    char target_hex[129];
    char query[768];
    size_t owner_len;
    size_t target_len;

    if (removedOut)
        *removedOut = false;
    if (ownerAccountId == NULL || ownerAccountId[0] == 0 || ownerRoleId == 0 ||
        targetAccountId == NULL || targetAccountId[0] == 0 || targetRoleId == 0)
    {
        return false;
    }
    owner_len = vm_mock_mysql_bounded_strlen(ownerAccountId, 64);
    target_len = vm_mock_mysql_bounded_strlen(targetAccountId, 64);
    if (owner_len == 0 || owner_len >= 64 || target_len == 0 || target_len >= 64 ||
        vm_mysql_hex_encode(ownerAccountId, owner_len, owner_hex, sizeof(owner_hex)) == 0 ||
        vm_mysql_hex_encode(targetAccountId, target_len, target_hex, sizeof(target_hex)) == 0)
    {
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM friendships WHERE "
             "(owner_account_id=CAST(X'%s' AS CHAR) AND owner_role_id=%u AND "
             "target_account_id=CAST(X'%s' AS CHAR) AND target_role_id=%u) OR "
             "(owner_account_id=CAST(X'%s' AS CHAR) AND owner_role_id=%u AND "
             "target_account_id=CAST(X'%s' AS CHAR) AND target_role_id=%u)",
             owner_hex, ownerRoleId, target_hex, targetRoleId,
             target_hex, targetRoleId, owner_hex, ownerRoleId);
    if (!vm_mysql_exec(query))
    {
        printf("[error][mock-service] friend_pair_delete_failed owner=%s/%u target=%s/%u error=%s\n",
               ownerAccountId, ownerRoleId, targetAccountId, targetRoleId,
               vm_mysql_last_error());
        return false;
    }
    if (removedOut)
        *removedOut = true;
    vm_autotest_note("mock_friend_db_mysql_delete_pair owner=%u target=%u\n",
                     ownerRoleId, targetRoleId);
    return true;
}

static bool vm_mock_service_friend_db_remove_pair(
    const char *ownerAccountId, u32 ownerRoleId, u32 targetRoleId,
    bool *removedOut)
{
    vm_mock_service_friend_record record;

    if (removedOut)
        *removedOut = false;
    if (ownerAccountId == NULL || ownerAccountId[0] == 0 || ownerRoleId == 0 ||
        targetRoleId == 0)
    {
        return false;
    }
    vm_mock_service_friend_db_load();
    if (!g_vm_mock_service_friend_db_valid && !vm_mock_service_mysql_authority_is_sealed())
        return false;
    memset(&record, 0, sizeof(record));
    if (!vm_mock_service_friend_db_mysql_find_owner_target(
            ownerAccountId, ownerRoleId, targetRoleId, &record) ||
        record.targetAccountId[0] == 0)
    {
        /* Not friends — treat as idempotent success for the client ack path. */
        return true;
    }
    return vm_mock_service_friend_db_mysql_delete_pair(
        ownerAccountId, ownerRoleId,
        record.targetAccountId, record.targetRoleId,
        removedOut);
}

/*
 * accounts.account_id is ascii_bin (case-sensitive).  Mobile IME overlays use
 * autocapitalize=off, so players often type aa862047 against a registered
 * AA862047.  Compare foldably for auth, but always return the stored spelling.
 */
static unsigned char vm_mock_service_ascii_tolower(unsigned char ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return (unsigned char)(ch - 'A' + 'a');
    return ch;
}

static int vm_mock_service_account_id_ascii_casecmp(const char *left, const char *right)
{
    size_t i = 0;

    if (left == NULL || right == NULL)
        return (left == right) ? 0 : (left == NULL ? -1 : 1);
    for (;;)
    {
        unsigned char a = vm_mock_service_ascii_tolower((unsigned char)left[i]);
        unsigned char b = vm_mock_service_ascii_tolower((unsigned char)right[i]);
        if (a != b)
            return (int)a - (int)b;
        if (a == 0)
            return 0;
        ++i;
    }
}

static bool vm_mock_service_account_id_equal(const char *left, const char *right)
{
    return vm_mock_service_account_id_ascii_casecmp(left, right) == 0;
}

static vm_mock_service_account_record *vm_mock_service_account_find_record(const char *username)
{
    char username_hex[sizeof(g_vm_mock_service_account_lookup_scratch.username) * 2 + 1];
    char query[448];
    size_t username_len;
    vm_mock_mysql_account_lookup_context context;

    vm_mock_service_account_db_load();
    if (!g_vm_mock_service_account_db_valid || username == NULL || username[0] == 0)
        return NULL;
    username_len = vm_mock_mysql_bounded_strlen(username,
                                                sizeof(g_vm_mock_service_account_lookup_scratch.username));
    if (username_len == 0 ||
        username_len >= sizeof(g_vm_mock_service_account_lookup_scratch.username) ||
        vm_mysql_hex_encode(username, username_len, username_hex, sizeof(username_hex)) == 0)
    {
        return NULL;
    }
    /*
     * Pass 0: exact ascii_bin match (preferred when both casings somehow exist).
     * Pass 1: LOWER() fold so mobile lowercase login still finds AA862047.
     * Cold MySQL connect / brief SO_RCVTIMEO can fail the first COM_QUERY.
     * Retry once on transport failure only — a clean "not found" must not retry
     * or a missing account would look like a transient glitch.
     */
    for (int pass = 0; pass < 2; ++pass)
    {
        bool transportFailed = false;

        if (pass == 0)
        {
            snprintf(query, sizeof(query),
                     "SELECT account_id,HEX(password_value) FROM accounts "
                     "WHERE account_id=CAST(X'%s' AS CHAR) LIMIT 1",
                     username_hex);
        }
        else
        {
            snprintf(query, sizeof(query),
                     "SELECT account_id,HEX(password_value) FROM accounts "
                     "WHERE LOWER(account_id)=LOWER(CAST(X'%s' AS CHAR)) LIMIT 1",
                     username_hex);
        }
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            memset(&g_vm_mock_service_account_lookup_scratch, 0,
                   sizeof(g_vm_mock_service_account_lookup_scratch));
            memset(&context, 0, sizeof(context));
            context.record = &g_vm_mock_service_account_lookup_scratch;
            if (!vm_mysql_query(query, vm_mock_mysql_account_lookup_row, &context))
            {
                printf("[warn][network] account_lookup_query_failed user=%s "
                       "pass=%d attempt=%d error=%s\n",
                       username,
                       pass,
                       attempt + 1,
                       vm_mysql_last_error());
                transportFailed = true;
                continue;
            }
            transportFailed = false;
            if (context.invalid || !context.found)
                break;
            if (pass == 1 &&
                strcmp(g_vm_mock_service_account_lookup_scratch.username, username) != 0)
            {
                printf("[warn][mock-service] account_case_canonicalized typed=%s "
                       "canonical=%s evidence=accounts-ascii-bin-login\n",
                       username,
                       g_vm_mock_service_account_lookup_scratch.username);
            }
            return &g_vm_mock_service_account_lookup_scratch;
        }
        if (transportFailed)
        {
            memset(&g_vm_mock_service_account_lookup_scratch, 0,
                   sizeof(g_vm_mock_service_account_lookup_scratch));
            return NULL;
        }
    }
    memset(&g_vm_mock_service_account_lookup_scratch, 0,
           sizeof(g_vm_mock_service_account_lookup_scratch));
    return NULL;
}

static bool vm_mock_service_account_verify_credentials(const char *username, const char *password)
{
    vm_mock_service_account_record *record = vm_mock_service_account_find_record(username);
    if (record == NULL || password == NULL)
        return false;
    return strcmp(record->password, password) == 0;
}

static bool vm_mock_service_account_copy_password(const char *username,
                                                  char *passwordOut,
                                                  size_t passwordOutCap)
{
    vm_mock_service_account_record *record = vm_mock_service_account_find_record(username);
    if (passwordOut == NULL || passwordOutCap == 0)
        return false;
    passwordOut[0] = 0;
    if (record == NULL)
        return false;
    snprintf(passwordOut, passwordOutCap, "%s", record->password);
    return passwordOut[0] != 0;
}

enum
{
    VM_MOCK_SERVICE_REGISTER_DAILY_LIMIT = 3
};

static u32 vm_mock_service_local_day_ymd(void)
{
    time_t now = time(NULL);
    struct tm *tmNow = localtime(&now);

    if (tmNow == NULL)
        return 0;
    return (u32)((tmNow->tm_year + 1900) * 10000 + (tmNow->tm_mon + 1) * 100 +
                 tmNow->tm_mday);
}

static bool vm_mock_service_register_daily_count_row(void *contextValue,
                                                     unsigned int columnCount,
                                                     const char *const *values,
                                                     const size_t *lengths)
{
    u32 *countOut = (u32 *)contextValue;
    u32 used = 0;

    (void)columnCount;
    if (countOut == NULL || values == NULL || lengths == NULL || values[0] == NULL ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &used))
        return false;
    *countOut = used;
    return true;
}

static bool vm_mock_service_register_daily_affected_row(void *contextValue,
                                                          unsigned int columnCount,
                                                          const char *const *values,
                                                          const size_t *lengths)
{
    u32 *affectedOut = (u32 *)contextValue;

    (void)columnCount;
    if (affectedOut == NULL || values == NULL || lengths == NULL || values[0] == NULL ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], affectedOut))
        return false;
    return true;
}

static bool vm_mock_service_register_daily_ensure_table(void)
{
    static bool ready = false;

    if (ready)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_registration_daily ("
            "day_ymd INT UNSIGNED NOT NULL,"
            "register_count INT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(day_ymd)) ENGINE=InnoDB"))
    {
        return false;
    }
    ready = true;
    return true;
}

static bool vm_mock_service_register_daily_exec_affected(const char *sql,
                                                         u32 *affectedOut)
{
    u32 affected = 0;

    if (affectedOut != NULL)
        *affectedOut = 0;
    if (sql == NULL || !vm_mysql_exec(sql))
        return false;
    if (!vm_mysql_query("SELECT ROW_COUNT()",
                        vm_mock_service_register_daily_affected_row, &affected))
        return false;
    if (affectedOut != NULL)
        *affectedOut = affected;
    return true;
}

static u32 vm_mock_service_register_daily_count(u32 dayYmd)
{
    char query[256];
    u32 used = 0;

    if (dayYmd == 0 || !vm_mock_service_register_daily_ensure_table())
        return 0;
    snprintf(query, sizeof(query),
             "SELECT register_count FROM server_registration_daily "
             "WHERE day_ymd=%u LIMIT 1",
             dayYmd);
    if (!vm_mysql_query(query, vm_mock_service_register_daily_count_row, &used))
        return 0;
    return used;
}

static void vm_mock_service_register_daily_release(void)
{
    u32 dayYmd = vm_mock_service_local_day_ymd();
    char query[256];

    if (dayYmd == 0 || !vm_mock_service_register_daily_ensure_table())
        return;
    snprintf(query, sizeof(query),
             "UPDATE server_registration_daily "
             "SET register_count=IF(register_count>0,register_count-1,0) "
             "WHERE day_ymd=%u",
             dayYmd);
    (void)vm_mysql_exec(query);
}

static bool vm_mock_service_register_daily_try_reserve(void)
{
    u32 dayYmd = vm_mock_service_local_day_ymd();
    u32 affected = 0;
    u32 count = 0;
    char query[384];

    if (dayYmd == 0 || !vm_mock_service_register_daily_ensure_table())
        return false;

    snprintf(query, sizeof(query),
             "INSERT INTO server_registration_daily(day_ymd,register_count) "
             "VALUES(%u,1) ON DUPLICATE KEY UPDATE "
             "register_count=IF(register_count<%u,register_count+1,register_count)",
             dayYmd, VM_MOCK_SERVICE_REGISTER_DAILY_LIMIT);
    if (!vm_mock_service_register_daily_exec_affected(query, &affected) ||
        affected == 0)
        return false;

    count = vm_mock_service_register_daily_count(dayYmd);
    if (count == 0 || count > VM_MOCK_SERVICE_REGISTER_DAILY_LIMIT)
    {
        vm_mock_service_register_daily_release();
        return false;
    }
    return true;
}

static bool vm_mock_service_account_create_record(const char *username,
                                                  const char *password,
                                                  const char **messageOut)
{
    vm_mock_service_account_record pending;

    vm_mock_service_account_db_load();
    if (messageOut)
        *messageOut = "ok";
    if (!g_vm_mock_service_account_db_valid)
    {
        if (messageOut)
            *messageOut = "account db unavailable";
        return false;
    }
    if (username == NULL || username[0] == 0 || password == NULL || password[0] == 0)
    {
        if (messageOut)
            *messageOut = "username/password cannot be empty";
        return false;
    }
    if (vm_mock_service_account_find_record(username) != NULL)
    {
        if (messageOut)
            *messageOut = "account already exists";
        return false;
    }
    memset(&pending, 0, sizeof(pending));
    snprintf(pending.username, sizeof(pending.username), "%s", username);
    snprintf(pending.password, sizeof(pending.password), "%s", password);
    if (!vm_mock_service_account_db_write_record(&pending, true, "create"))
    {
        if (messageOut)
            *messageOut = "account persistence failed";
        return false;
    }
    return true;
}

static bool vm_mock_service_account_register_user(const char *username,
                                                  const char *password,
                                                  const char **messageOut)
{
    if (messageOut)
        *messageOut = "ok";
    if (!vm_mock_service_register_daily_try_reserve())
    {
        if (messageOut)
            *messageOut = "daily registration limit reached";
        return false;
    }
    if (!vm_mock_service_account_create_record(username, password, messageOut))
    {
        vm_mock_service_register_daily_release();
        return false;
    }
    printf("[info][mock-service] account_register_user account=%s day=%u "
           "daily_used=%u/%u\n",
           username ? username : "-", vm_mock_service_local_day_ymd(),
           vm_mock_service_register_daily_count(vm_mock_service_local_day_ymd()),
           VM_MOCK_SERVICE_REGISTER_DAILY_LIMIT);
    return true;
}

static bool vm_mock_service_account_set_password(const char *username,
                                                 const char *password,
                                                 const char **messageOut)
{
    vm_mock_service_account_record *record = vm_mock_service_account_find_record(username);
    vm_mock_service_account_record pending;

    if (messageOut)
        *messageOut = "ok";
    if (record == NULL)
    {
        if (messageOut)
            *messageOut = "account not found";
        return false;
    }
    if (password == NULL || password[0] == 0)
    {
        if (messageOut)
            *messageOut = "password cannot be empty";
        return false;
    }
    pending = *record;
    snprintf(pending.password, sizeof(pending.password), "%s", password);
    if (!vm_mock_service_account_db_write_record(&pending, false, "passwd"))
    {
        if (messageOut)
            *messageOut = "account persistence failed";
        return false;
    }
    return true;
}

static bool vm_mock_service_account_issue_guest_credentials(u32 clientId,
                                                            char *usernameOut,
                                                            size_t usernameOutCap,
                                                            char *passwordOut,
                                                            size_t passwordOutCap,
                                                            const char **messageOut)
{
    u32 seedBase = 0;
    u32 account_count = 0;
    const char *message = "account db unavailable";

    if (messageOut)
        *messageOut = message;
    if (usernameOut == NULL || usernameOutCap == 0 || passwordOut == NULL || passwordOutCap == 0)
        return false;
    usernameOut[0] = 0;
    passwordOut[0] = 0;

    vm_mock_service_account_db_load();
    if (!g_vm_mock_service_account_db_valid ||
        !vm_mock_service_account_count(&account_count))
    {
        return false;
    }

    seedBase = account_count + 1;
    for (u32 attempt = 0; attempt < 100000; ++attempt)
    {
        u32 ordinal = seedBase + attempt;
        snprintf(usernameOut, usernameOutCap, "guest%05u", ordinal);
        snprintf(passwordOut, passwordOutCap, "g%08X", clientId ^ (ordinal * 2654435761u));
        if (vm_mock_service_account_find_record(usernameOut) != NULL)
            continue;
        if (vm_mock_service_account_create_record(usernameOut, passwordOut, &message))
        {
            if (messageOut)
                *messageOut = "ok";
            return true;
        }
        if (message != NULL && strcmp(message, "account already exists") != 0)
            break;
    }

    if (messageOut)
        *messageOut = message;
    usernameOut[0] = 0;
    passwordOut[0] = 0;
    return false;
}

/* EXP to advance from level L -> L+1 for L=1..49.
 * Geometric progression: 200 * r^(L-1) with r?1.2115, nudged to keep
 * integer monotonic steps, max step growth ? ~22%, exact 49->50 = 2_000_000,
 * and sum(1..49) = 11_454_020 so stage 3 can lock total 1..70 at 120_000_000. */
static const u32 g_vm_net_mock_role_level_up_cost_geom[] = {
    200, 240, 292, 354, 429, 520, 630, 764,
    926, 1123, 1361, 1649, 1998, 2421, 2934, 3555,
    4307, 5218, 6323, 7660, 9282, 11246, 13625, 16507,
    19999, 24230, 29355, 35565, 43088, 52202, 63245, 76623,
    92831, 112467, 136257, 165080, 199999, 242305, 293559, 355655,
    430886, 522030, 632455, 766236, 928317, 1124682, 1362583, 1650807,
    2000000
};

/* EXP cost to advance from `level` to `level + 1`.
 *
 * Stages 1-2 (levels 1..49): smooth geometric table above.
 * Stage 3 (levels 50+): arithmetic a=2_100_000, d=350_242.
 * Anchors: 1->2=200, 49->50=2_000_000, sum(1->70)=120_000_000. */
static u32 vm_net_mock_role_level_up_cost(u32 level)
{
    unsigned long long cost;

    if (level < 1u)
        return 0;

    if (level <= (u32)VM_NET_MOCK_ROLE_EXP_GEOM_LAST_LEVEL)
    {
        if (level > (u32)(sizeof(g_vm_net_mock_role_level_up_cost_geom) /
                          sizeof(g_vm_net_mock_role_level_up_cost_geom[0])))
            return 0xffffffffu;
        return g_vm_net_mock_role_level_up_cost_geom[level - 1u];
    }

    cost = (unsigned long long)VM_NET_MOCK_ROLE_EXP_STAGE3_A +
           (unsigned long long)(level - (u32)VM_NET_MOCK_ROLE_EXP_STAGE3_FIRST_LEVEL) *
               (unsigned long long)VM_NET_MOCK_ROLE_EXP_STAGE3_D;
    if (cost > 0xffffffffull)
        return 0xffffffffu;
    return (u32)cost;
}

static u32 vm_net_mock_role_level_start_exp(u32 level)
{
    unsigned long long startExp = 0;
    u32 fromLevel;

    if (level <= 1u)
        return 0;

    for (fromLevel = 1u; fromLevel < level; ++fromLevel)
    {
        u32 step = vm_net_mock_role_level_up_cost(fromLevel);

        if (step == 0xffffffffu || startExp > 0xffffffffull - (unsigned long long)step)
            return 0xffffffffu;
        startExp += (unsigned long long)step;
    }

    return (u32)startExp;
}

static u32 vm_net_mock_role_level_from_exp(u32 exp)
{
    u32 level = 1;

    for (;;)
    {
        u32 nextLevel = level + 1;
        u32 nextLevelStart;

        if (nextLevel == 0 || nextLevel > (u32)VM_NET_MOCK_ROLE_MAX_LEVEL)
            break;
        nextLevelStart = vm_net_mock_role_level_start_exp(nextLevel);
        if (nextLevelStart == 0xffffffffu || exp < nextLevelStart)
            break;
        level = nextLevel;
    }

    return level;
}

static u32 vm_net_mock_role_last_level_exp(u32 exp)
{
    return vm_net_mock_role_level_start_exp(vm_net_mock_role_level_from_exp(exp));
}

static u32 vm_net_mock_role_next_level_start_exp(u32 exp)
{
    u32 level = vm_net_mock_role_level_from_exp(exp);
    u32 nextLevel = level + 1;

    if (nextLevel == 0 || level >= (u32)VM_NET_MOCK_ROLE_MAX_LEVEL)
        return 0xffffffffu;
    return vm_net_mock_role_level_start_exp(nextLevel);
}

static u32 vm_net_mock_role_exp_percent(u32 exp)
{
    u32 levelStart = vm_net_mock_role_last_level_exp(exp);
    u32 nextLevelStart = vm_net_mock_role_next_level_start_exp(exp);
    u32 levelSize = 0;
    u32 current = 0;

    if (exp <= levelStart)
        return 0;
    if (nextLevelStart <= levelStart || nextLevelStart == 0xffffffffu)
        return 100;

    current = exp - levelStart;
    levelSize = nextLevelStart - levelStart;
    return (u32)(((unsigned long long)current * 100ull) / levelSize);
}

static const char *vm_net_mock_default_role_name(void)
{
    return "\xcf\xc0\xbd\xa3\xbd\xad\xba\xfe"; /* GBK: xia jian jiang hu */
}

typedef enum
{
    VM_NET_MOCK_DESIGNATION_KIND_MONEY = 0,
    VM_NET_MOCK_DESIGNATION_KIND_LEVEL = 1,
    /* Pixi-only：比武连杀文字称号（overhead 空串，客户端画字）。 */
    VM_NET_MOCK_DESIGNATION_KIND_ARENA = 2,
    /* 装备套装 / 创建时间永久称号（specialDesignationMask）。 */
    VM_NET_MOCK_DESIGNATION_KIND_SPECIAL = 3,
} vm_net_mock_designation_kind;

/* 资深老友门槛：角色 created_at < 2026-08-16 00:00:00 Asia/Shanghai。 */
enum
{
    VM_NET_MOCK_SENIOR_FRIEND_CUTOFF_UNIX = 1786809600u,
    VM_NET_MOCK_DESIGNATION_ID_CHRISTMAS_KNIGHT = 41,
    VM_NET_MOCK_DESIGNATION_ID_WULIN_LEGEND = 42,
    VM_NET_MOCK_DESIGNATION_ID_SENIOR_HERO = 43,
    VM_NET_MOCK_DESIGNATION_ID_SENIOR_FRIEND = 44,
    VM_NET_MOCK_DESIGNATION_ID_HERO_KING = 45,
};

typedef struct
{
    u8 id;
    u8 fieldB;
    u8 kind;
    u32 minMoney;
    u32 minLevel;
    const char *name;
    const char *description;
    const char *overheadResource;
} vm_net_mock_designation_entry;

static const vm_net_mock_designation_entry g_vm_net_mock_designation_entries[] = {
    /*
     * Wealth titles: riches_name0.gif .. riches_name9.gif (resource order fixed).
     * Thresholds are copper; UI gold = money/10000 (admin + shop contract).
     * Config gold mins: 0/10/100/500/1000/10000/50000/100000/150000/200000.
     */
    {
        0,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_MONEY,
        0,
        0,
        "\xd2\xbb\xc6\xb6\xc8\xe7\xcf\xb4", /* GBK: ???? */
        "\xc5\xcc\xb2\xf8\xb2\xbb\xd7\xe3", /* GBK: ???? */
        "riches_name0.gif",
    },
    {
        1,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_MONEY,
        10u * 10000u, /* 10 gold */
        0,
        "\xd2\xc2\xca\xb3\xce\xde\xd3\xc7", /* GBK: ???? */
        "\xc2\xd4\xd3\xd0\xbb\xfd\xd0\xee", /* GBK: ???? */
        "riches_name1.gif",
    },
    {
        2,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_MONEY,
        100u * 10000u, /* 100 gold */
        0,
        "\xc9\xfa\xb2\xc6\xd3\xd0\xb5\xc0", /* GBK: ???? */
        "\xd0\xa1\xd3\xd0\xd7\xca\xb2\xfa", /* GBK: ???? */
        "riches_name2.gif",
    },
    {
        3,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_MONEY,
        500u * 10000u, /* 500 gold */
        0,
        "\xc0\xed\xb2\xc6\xd3\xd0\xb7\xbd", /* GBK: ???? */
        "\xb2\xc6\xc2\xb7\xbd\xa5\xbf\xed", /* GBK: ???? */
        "riches_name3.gif",
    },
    {
        4,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_MONEY,
        1000u * 10000u, /* 1000 gold */
        0,
        "\xb2\xc6\xd4\xcb\xba\xe0\xcd\xa8", /* GBK: ???? */
        "\xc7\xae\xb2\xc6\xb7\xe1\xba\xf1", /* GBK: ???? */
        "riches_name4.gif",
    },
    {
        5,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_MONEY,
        10000u * 10000u, /* 10000 gold */
        0,
        "\xd1\xfc\xb2\xf8\xcd\xf2\xb9\xe1", /* GBK: ???? */
        "\xbb\xd3\xbd\xf0\xd3\xd0\xb6\xc8", /* GBK: ???? */
        "riches_name5.gif",
    },
    {
        6,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_MONEY,
        50000u * 10000u, /* 50000 gold */
        0,
        "\xbc\xd2\xb2\xc6\xcd\xf2\xb9\xe1", /* GBK: ???? */
        "\xb2\xc6\xb8\xbb\xbe\xaa\xc8\xcb", /* GBK: ???? */
        "riches_name6.gif",
    },
    {
        7,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_MONEY,
        100000u * 10000u, /* 100000 gold */
        0,
        "\xb8\xbb\xc9\xcc\xbe\xde\xbc\xd6", /* GBK: ???? */
        "\xc9\xcc\xbc\xd6\xce\xc5\xc3\xfb", /* GBK: ???? */
        "riches_name7.gif",
    },
    {
        8,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_MONEY,
        150000u * 10000u, /* 150000 gold */
        0,
        "\xb8\xbb\xbc\xd7\xd2\xbb\xb7\xbd", /* GBK: ???? */
        "\xb2\xc6\xb9\xda\xd2\xbb\xb7\xbd", /* GBK: ???? */
        "riches_name8.gif",
    },
    {
        9,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_MONEY,
        200000u * 10000u, /* 200000 gold */
        0,
        "\xb8\xbb\xbf\xc9\xb5\xd0\xb9\xfa", /* GBK: ???? */
        "\xcc\xec\xcf\xc2\xbe\xde\xb8\xbb", /* GBK: ???? */
        "riches_name9.gif",
    },
    /*
     * Level titles: level_name0.gif .. level_name12.gif (resource order fixed).
     * Thresholds from admin title config (min level of each band).
     * fieldB stays 0: nonzero crashed scene_draw_actor_pass after 23/2.
     */
    {
        10,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_LEVEL,
        0,
        0,
        "\xb2\xbb\xbf\xb0\xd2\xbb\xbb\xf7", /* GBK: ???? */
        "\xb3\xf5\xc8\xeb\xbd\xad\xba\xfe", /* GBK: ???? */
        "level_name0.gif",
    },
    {
        11,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_LEVEL,
        0,
        5,
        "\xb3\xf5\xd1\xa7\xd5\xa7\xc1\xb7", /* GBK: ???? */
        "\xc2\xd4\xcd\xa8\xce\xe4\xd1\xa7", /* GBK: ???? */
        "level_name1.gif",
    },
    {
        12,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_LEVEL,
        0,
        10,
        "\xd0\xa1\xca\xd4\xc5\xa3\xb5\xb6", /* GBK: ???? */
        "\xbd\xa5\xc8\xeb\xbc\xd1\xbe\xb3", /* GBK: ???? */
        "level_name2.gif",
    },
    {
        13,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_LEVEL,
        0,
        15,
        "\xb3\xf5\xc2\xb6\xb7\xe6\xc3\xa2", /* GBK: ???? */
        "\xd0\xa1\xd3\xd0\xcb\xf9\xb3\xc9", /* GBK: ???? */
        "level_name3.gif",
    },
    {
        14,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_LEVEL,
        0,
        25,
        "\xb3\xf6\xc8\xcb\xcd\xb7\xb5\xd8", /* GBK: ???? */
        "\xc9\xf9\xc3\xfb\xbd\xa5\xc6\xf0", /* GBK: ???? */
        "level_name4.gif",
    },
    {
        15,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_LEVEL,
        0,
        30,
        "\xc3\xfb\xd5\xf0\xbd\xad\xba\xfe", /* GBK: ???? */
        "\xc9\xf9\xc3\xfb\xd4\xb6\xd1\xef", /* GBK: ???? */
        "level_name5.gif",
    },
    {
        16,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_LEVEL,
        0,
        40,
        "\xbd\xad\xba\xfe\xe8\xc9\xd0\xdb", /* GBK: ???? */
        "\xcd\xfe\xd5\xf0\xd2\xbb\xb7\xbd", /* GBK: ???? */
        "level_name6.gif",
    },
    {
        17,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_LEVEL,
        0,
        45,
        "\xc1\xcb\xc8\xbb\xd3\xda\xd0\xd8", /* GBK: ???? */
        "\xb5\xc3\xd0\xc4\xd3\xa6\xca\xd6", /* GBK: ???? */
        "level_name7.gif",
    },
    {
        18,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_LEVEL,
        0,
        50,
        "\xc2\xaf\xbb\xf0\xb4\xbf\xc7\xe0", /* GBK: ???? */
        "\xb9\xa6\xc1\xa6\xc9\xee\xba\xf1", /* GBK: ???? */
        "level_name8.gif",
    },
    {
        19,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_LEVEL,
        0,
        55,
        "\xbd\xad\xba\xfe\xcf\xc0\xd2\xfe", /* GBK: ???? */
        "\xcf\xc0\xd2\xfe\xbd\xad\xba\xfe", /* GBK: ???? */
        "level_name9.gif",
    },
    {
        20,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_LEVEL,
        0,
        60,
        "\xb5\xc7\xb7\xe5\xd4\xec\xbc\xab", /* GBK: ???? */
        "\xce\xe4\xd1\xa7\xe1\xdb\xb7\xe5", /* GBK: ???? */
        "level_name10.gif",
    },
    {
        21,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_LEVEL,
        0,
        65,
        "\xb3\xac\xd4\xbd\xbc\xab\xcf\xde", /* GBK: ???? */
        "\xb3\xac\xb7\xb2\xc8\xeb\xca\xa5", /* GBK: ???? */
        "level_name11.gif",
    },
    {
        22,
        0,
        VM_NET_MOCK_DESIGNATION_KIND_LEVEL,
        0,
        70,
        "\xbf\xaa\xc9\xbd\xb1\xc7\xd7\xe6", /* GBK: ???? */
        "\xd2\xbb\xb4\xfa\xd7\xda\xca\xa6", /* GBK: ???? */
        "level_name12.gif",
    },
    /*
     * Arena kill-streak text titles (Pixi): id 30–40, empty overhead —
     * client draws the display name. Unlock via arenaDesignationMask bits.
     */
    {
        30, 0, VM_NET_MOCK_DESIGNATION_KIND_ARENA, 0, 0,
        "\xb5\xd0\xbd\xab\xcc\xd6\xb7\xa5", /* 敌将讨伐 */
        "\xb1\xc8\xce\xe4\xc1\xac\xca\xa4\xb3\xc6\xba\xc5", /* 比武连胜称号 */
        "",
    },
    {
        31, 0, VM_NET_MOCK_DESIGNATION_KIND_ARENA, 0, 0,
        "\xcb\xab\xc9\xb1", /* 双杀 */
        "\xb1\xc8\xce\xe4\xc1\xac\xca\xa4\xb3\xc6\xba\xc5",
        "",
    },
    {
        32, 0, VM_NET_MOCK_DESIGNATION_KIND_ARENA, 0, 0,
        "\xb4\xf3\xc9\xb1\xcc\xd8\xc9\xb1", /* 大杀特杀 */
        "\xb1\xc8\xce\xe4\xc1\xac\xca\xa4\xb3\xc6\xba\xc5",
        "",
    },
    {
        33, 0, VM_NET_MOCK_DESIGNATION_KIND_ARENA, 0, 0,
        "\xcb\xf9\xcf\xf2\xc5\xfb\xc3\xd2", /* 所向披靡 */
        "\xb1\xc8\xce\xe4\xc1\xac\xca\xa4\xb3\xc6\xba\xc5",
        "",
    },
    {
        34, 0, VM_NET_MOCK_DESIGNATION_KIND_ARENA, 0, 0,
        "\xbb\xeb\xc9\xed\xca\xc7\xb5\xa8", /* 浑身是胆 */
        "\xb1\xc8\xce\xe4\xc1\xac\xca\xa4\xb3\xc6\xba\xc5",
        "",
    },
    {
        35, 0, VM_NET_MOCK_DESIGNATION_KIND_ARENA, 0, 0,
        "\xca\xc6\xc8\xe7\xc6\xc6\xd6\xf1", /* 势如破竹 */
        "\xb1\xc8\xce\xe4\xc1\xac\xca\xa4\xb3\xc6\xba\xc5",
        "",
    },
    {
        36, 0, VM_NET_MOCK_DESIGNATION_KIND_ARENA, 0, 0,
        "\xd3\xc2\xb9\xda\xc8\xfd\xbe\xfc", /* 勇冠三军 */
        "\xb1\xc8\xce\xe4\xc1\xac\xca\xa4\xb3\xc6\xba\xc5",
        "",
    },
    {
        37, 0, VM_NET_MOCK_DESIGNATION_KIND_ARENA, 0, 0,
        "\xd2\xbb\xc6\xef\xb5\xb1\xc7\xa7", /* 一骑当千 */
        "\xb1\xc8\xce\xe4\xc1\xac\xca\xa4\xb3\xc6\xba\xc5",
        "",
    },
    {
        38, 0, VM_NET_MOCK_DESIGNATION_KIND_ARENA, 0, 0,
        "\xcd\xf2\xb7\xf2\xc4\xaa\xb5\xd0", /* 万夫莫敌 */
        "\xb1\xc8\xce\xe4\xc1\xac\xca\xa4\xb3\xc6\xba\xc5",
        "",
    },
    {
        39, 0, VM_NET_MOCK_DESIGNATION_KIND_ARENA, 0, 0,
        "\xbe\xd9\xca\xc0\xce\xde\xcb\xab", /* 举世无双 */
        "\xb1\xc8\xce\xe4\xc1\xac\xca\xa4\xb3\xc6\xba\xc5",
        "",
    },
    {
        40, 0, VM_NET_MOCK_DESIGNATION_KIND_ARENA, 0, 0,
        "\xcc\xec\xcf\xc2\xce\xde\xb5\xd0", /* 天下无敌 */
        "\xb1\xc8\xce\xe4\xc1\xac\xca\xa4\xb3\xc6\xba\xc5",
        "",
    },
    /*
     * Special permanent titles (id 41–45): unlock once via equipment / create
     * time, then stay unlocked even after unequip (specialDesignationMask).
     */
    {
        41, 0, VM_NET_MOCK_DESIGNATION_KIND_SPECIAL, 0, 0,
        "\xca\xa5\xb5\xae\xc6\xef\xca\xbf", /* 圣诞骑士 */
        "\xb4\xa9\xb4\xf7\xc8\xab\xcc\xd7\xca\xa5\xb5\xae\xd7\xb0\xb1\xb8\xd3\xc0\xbe\xc3\xbc\xa4\xbb\xee",
        "sdqs0.gif",
    },
    {
        42, 0, VM_NET_MOCK_DESIGNATION_KIND_SPECIAL, 0, 0,
        "\xce\xe4\xc1\xd6\xb4\xab\xc6\xe6", /* 武林传奇 */
        "\xb4\xa9\xb4\xf7\xc8\xab\xcc\xd7\xce\xe4\xc1\xd6\xd7\xb0\xb1\xb8\xd3\xc0\xbe\xc3\xbc\xa4\xbb\xee",
        "wlcq0.gif",
    },
    {
        43, 0, VM_NET_MOCK_DESIGNATION_KIND_SPECIAL, 0, 0,
        "\xd7\xca\xc9\xee\xc0\xcf\xd3\xd1\xd3\xc2\xd5\xdf\xcd\xf5", /* 资深老友勇者王 */
        "\xb4\xa9\xb4\xf7\xc3\xce\xbe\xb3\xce\xe4\xc6\xf7\xb2\xa2\xc7\xd2\xb4\xb4\xbd\xa8\xbd\xc7\xc9\xab"
        "\xca\xb1\xbc\xe4\xd4\xda\x32\x30\x32\x36\x2d\x30\x38\x2d\x31\x36\xd6\xae\xc7\xb0"
        "\xd3\xc0\xbe\xc3\xbc\xa4\xbb\xee",
        "zsly.gif",
    },
    {
        44, 0, VM_NET_MOCK_DESIGNATION_KIND_SPECIAL, 0, 0,
        "\xd7\xca\xc9\xee\xc0\xcf\xd3\xd1", /* 资深老友（需求原文「姿深」按笔误修正） */
        "\xb4\xb4\xbd\xa8\xbd\xc7\xc9\xab\xca\xb1\xbc\xe4\xd4\xda\x32\x30\x32\x36\x2d\x30\x38\x2d\x31\x36"
        "\xd6\xae\xc7\xb0\xd3\xc0\xbe\xc3\xbc\xa4\xbb\xee",
        "zsly0.gif",
    },
    {
        45, 0, VM_NET_MOCK_DESIGNATION_KIND_SPECIAL, 0, 0,
        "\xd3\xc2\xd5\xdf\xcd\xf5", /* 勇者王 */
        "\xb4\xa9\xb4\xf7\xc3\xce\xbe\xb3\xce\xe4\xc6\xf7\xd3\xc0\xbe\xc3\xbc\xa4\xbb\xee",
        "zsly1.gif",
    },

};

static u32 vm_net_mock_designation_entry_count(void)
{
    return (u32)(sizeof(g_vm_net_mock_designation_entries) /
                 sizeof(g_vm_net_mock_designation_entries[0]));
}

static const vm_net_mock_designation_entry *vm_net_mock_designation_by_id(u8 id)
{
    u32 count = vm_net_mock_designation_entry_count();
    for (u32 i = 0; i < count; ++i)
    {
        if (g_vm_net_mock_designation_entries[i].id == id)
            return &g_vm_net_mock_designation_entries[i];
    }
    return &g_vm_net_mock_designation_entries[0];
}

static bool vm_net_mock_designation_is_arena_id(u8 id)
{
    return id >= 30 && id <= 40;
}

static bool vm_net_mock_designation_is_special_id(u8 id)
{
    return id >= (u8)VM_NET_MOCK_DESIGNATION_ID_CHRISTMAS_KNIGHT &&
           id <= (u8)VM_NET_MOCK_DESIGNATION_ID_HERO_KING;
}

static u8 vm_net_mock_arena_designation_id_for_streak(u32 streak)
{
    if (streak == 0)
        return 0;
    if (streak >= 11u)
        return 40;
    return (u8)(29u + streak);
}

static u32 vm_net_mock_arena_designation_mask_bit(u8 id)
{
    if (!vm_net_mock_designation_is_arena_id(id))
        return 0;
    return 1u << (id - 30u);
}

static u32 vm_net_mock_special_designation_mask_bit(u8 id)
{
    if (!vm_net_mock_designation_is_special_id(id))
        return 0;
    return 1u << (id - (u8)VM_NET_MOCK_DESIGNATION_ID_CHRISTMAS_KNIGHT);
}

static bool vm_net_mock_role_wearing_item_id(const vm_net_mock_role_state *role,
                                             u32 itemId)
{
    if (role == NULL || itemId == 0)
        return false;
    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        if (role->equippedItemIds[slot] == itemId)
            return true;
    }
    return false;
}

static bool vm_net_mock_item_id_in_list(u32 itemId, const u32 *ids, u32 count)
{
    if (itemId == 0 || ids == NULL)
        return false;
    for (u32 i = 0; i < count; ++i)
    {
        if (ids[i] == itemId)
            return true;
    }
    return false;
}

/*
 * 全套：防具类别 1–6 每类穿同套任意一件（同类别可有多件变体）+
 * 武器类别 7/8/9 任意一件。
 * weaponMustBeFromSet=true 时武器也必须属于套装 ID 表；
 * false 时任意已穿戴的 7/8/9 武器即可（传奇套无专属武器）。
 */
static bool vm_net_mock_role_wearing_themed_full_set(
    const vm_net_mock_role_state *role,
    const u32 *setIds,
    u32 setCount,
    bool weaponMustBeFromSet)
{
    bool armorCatCovered[7];
    bool hasSetWeapon = false;
    bool hasAnyWeapon = false;

    if (role == NULL || setIds == NULL || setCount == 0)
        return false;
    memset(armorCatCovered, 0, sizeof(armorCatCovered));

    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        u32 itemId = role->equippedItemIds[slot];
        const vm_net_mock_equipment_catalog_item *equip;
        u8 category;
        bool inSet;

        if (itemId == 0)
            continue;
        equip = vm_net_mock_find_equipment_catalog_item(itemId);
        if (equip == NULL)
            continue;
        category = equip->category;
        inSet = vm_net_mock_item_id_in_list(itemId, setIds, setCount);
        if (category >= 1u && category <= 6u)
        {
            if (inSet)
                armorCatCovered[category] = true;
        }
        else if (category >= 7u && category <= 9u)
        {
            hasAnyWeapon = true;
            if (inSet)
                hasSetWeapon = true;
        }
    }

    for (u8 cat = 1; cat <= 6; ++cat)
    {
        if (!armorCatCovered[cat])
            return false;
    }
    if (weaponMustBeFromSet)
        return hasSetWeapon;
    return hasAnyWeapon;
}

static bool vm_net_mock_role_wearing_christmas_full_set(const vm_net_mock_role_state *role)
{
    static const u32 kChristmasIds[] = {
        40020u, 40021u, 40022u, 40023u, 40024u, 40025u, 40026u, 40027u, 40028u,
        40029u,
    };
    return vm_net_mock_role_wearing_themed_full_set(
        role, kChristmasIds,
        (u32)(sizeof(kChristmasIds) / sizeof(kChristmasIds[0])), true);
}

static bool vm_net_mock_role_wearing_wulin_full_set(const vm_net_mock_role_state *role)
{
    /* 武林套 40150–40159：防具 1–6 + 武林武器三选一（40150/40151/40152）。 */
    static const u32 kWulinIds[] = {
        40150u, 40151u, 40152u, 40153u, 40154u, 40155u, 40156u, 40157u, 40158u,
        40159u,
    };
    return vm_net_mock_role_wearing_themed_full_set(
        role, kWulinIds, (u32)(sizeof(kWulinIds) / sizeof(kWulinIds[0])), true);
}

static bool vm_net_mock_role_wearing_dream_weapon(const vm_net_mock_role_state *role)
{
    return vm_net_mock_role_wearing_item_id(role, 40065u) ||
           vm_net_mock_role_wearing_item_id(role, 40066u) ||
           vm_net_mock_role_wearing_item_id(role, 40067u);
}

static bool vm_net_mock_role_is_senior_friend_created(const vm_net_mock_role_state *role)
{
    return role != NULL && role->createdAtUnix != 0 &&
           role->createdAtUnix < (u32)VM_NET_MOCK_SENIOR_FRIEND_CUTOFF_UNIX;
}

static bool vm_net_mock_role_try_unlock_special_designation(vm_net_mock_role_state *role,
                                                            u8 id,
                                                            bool conditionMet,
                                                            const char *reason)
{
    u32 bit;

    if (role == NULL || !conditionMet)
        return false;
    bit = vm_net_mock_special_designation_mask_bit(id);
    if (bit == 0 || (role->specialDesignationMask & bit) != 0)
        return false;
    role->specialDesignationMask |= bit;
    printf("[info][network] mock_special_designation_unlock role=%u id=%u mask=0x%x reason=%s\n",
           role->roleId, id, role->specialDesignationMask,
           reason ? reason : "special");
    return true;
}

static bool vm_net_mock_role_refresh_special_designations(vm_net_mock_role_state *role)
{
    bool dirty = false;
    bool dream;
    bool senior;

    if (role == NULL)
        return false;
    dream = vm_net_mock_role_wearing_dream_weapon(role);
    senior = vm_net_mock_role_is_senior_friend_created(role);
    if (vm_net_mock_role_try_unlock_special_designation(
            role, (u8)VM_NET_MOCK_DESIGNATION_ID_CHRISTMAS_KNIGHT,
            vm_net_mock_role_wearing_christmas_full_set(role),
            "christmas-full-set"))
        dirty = true;
    if (vm_net_mock_role_try_unlock_special_designation(
            role, (u8)VM_NET_MOCK_DESIGNATION_ID_WULIN_LEGEND,
            vm_net_mock_role_wearing_wulin_full_set(role),
            "wulin-full-set"))
        dirty = true;
    if (vm_net_mock_role_try_unlock_special_designation(
            role, (u8)VM_NET_MOCK_DESIGNATION_ID_SENIOR_FRIEND, senior,
            "senior-friend-created-before-cutoff"))
        dirty = true;
    if (vm_net_mock_role_try_unlock_special_designation(
            role, (u8)VM_NET_MOCK_DESIGNATION_ID_HERO_KING, dream,
            "dream-weapon"))
        dirty = true;
    if (vm_net_mock_role_try_unlock_special_designation(
            role, (u8)VM_NET_MOCK_DESIGNATION_ID_SENIOR_HERO, dream && senior,
            "dream-weapon+senior-friend"))
        dirty = true;
    if (dirty)
        vm_net_mock_role_mark_inventory_dirty("special-designation-unlock");
    return dirty;
}

static bool vm_net_mock_designation_is_unlocked(const vm_net_mock_role_state *role,
                                                const vm_net_mock_designation_entry *entry)
{
    u32 money = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    u32 level = role ? role->level : 1;
    if (entry == NULL)
        return false;
    if (entry->kind == VM_NET_MOCK_DESIGNATION_KIND_ARENA)
    {
        u32 bit = vm_net_mock_arena_designation_mask_bit(entry->id);
        return bit != 0 && role != NULL &&
               (role->arenaDesignationMask & bit) != 0;
    }
    if (entry->kind == VM_NET_MOCK_DESIGNATION_KIND_SPECIAL)
    {
        u32 bit = vm_net_mock_special_designation_mask_bit(entry->id);
        return bit != 0 && role != NULL &&
               (role->specialDesignationMask & bit) != 0;
    }
    if (entry->kind == VM_NET_MOCK_DESIGNATION_KIND_LEVEL)
        return level >= entry->minLevel;
    return money >= entry->minMoney;
}

static const vm_net_mock_designation_entry *vm_net_mock_role_best_designation_of_kind(
    const vm_net_mock_role_state *role,
    u8 kind)
{
    const vm_net_mock_designation_entry *best = NULL;
    u32 count = vm_net_mock_designation_entry_count();
    for (u32 i = 0; i < count; ++i)
    {
        const vm_net_mock_designation_entry *entry = &g_vm_net_mock_designation_entries[i];
        if (entry->kind != kind)
            continue;
        if (vm_net_mock_designation_is_unlocked(role, entry))
            best = entry;
    }
    return best;
}

static const vm_net_mock_designation_entry *vm_net_mock_role_best_designation(const vm_net_mock_role_state *role)
{
    const vm_net_mock_designation_entry *current =
        vm_net_mock_designation_by_id(role ? role->designationId : 0);
    const vm_net_mock_designation_entry *best =
        vm_net_mock_role_best_designation_of_kind(role, current->kind);
    if (best != NULL)
        return best;
    /* Prefer durable wealth/level titles over transient arena text titles. */
    best = vm_net_mock_role_best_designation_of_kind(role, VM_NET_MOCK_DESIGNATION_KIND_MONEY);
    if (best != NULL)
        return best;
    best = vm_net_mock_role_best_designation_of_kind(role, VM_NET_MOCK_DESIGNATION_KIND_LEVEL);
    if (best != NULL)
        return best;
    best = vm_net_mock_role_best_designation_of_kind(role, VM_NET_MOCK_DESIGNATION_KIND_SPECIAL);
    if (best != NULL)
        return best;
    best = vm_net_mock_role_best_designation_of_kind(role, VM_NET_MOCK_DESIGNATION_KIND_ARENA);
    if (best != NULL)
        return best;
    return &g_vm_net_mock_designation_entries[0];
}

static const vm_net_mock_designation_entry *vm_net_mock_role_designation(const vm_net_mock_role_state *role)
{
    const vm_net_mock_designation_entry *entry = vm_net_mock_designation_by_id(role ? role->designationId : 0);
    if (vm_net_mock_designation_is_unlocked(role, entry))
        return entry;
    return vm_net_mock_role_best_designation(role);
}

static const char *vm_net_mock_role_title(const vm_net_mock_role_state *role)
{
    return vm_net_mock_role_designation(role)->name;
}

static u32 vm_net_mock_role_guild_info(const vm_net_mock_role_state *role,
                                       char *nameOut,
                                       size_t nameOutSize)
{
    const char *overrideName = vm_net_mock_env_str("CBE_ACTOR_SECT_NAME", "");
    vm_net_mock_guild_record guild;
    u32 guildId = 0;

    if (nameOut == NULL || nameOutSize == 0)
        return 0;
    nameOut[0] = 0;
    memset(&guild, 0, sizeof(guild));
    if (role != NULL &&
        vm_net_mock_guild_find_role_membership(role->roleId, &guild, NULL) &&
        guild.guildName[0] != 0)
    {
        snprintf(nameOut, nameOutSize, "%s", guild.guildName);
        guildId = guild.guildId;
    }
    else
    {
        snprintf(nameOut, nameOutSize, "%s", "\xce\xde\xb0\xef\xc5\xc9"); /* GBK: ??? */
    }
    if (overrideName != NULL && overrideName[0] != 0)
        snprintf(nameOut, nameOutSize, "%s", overrideName);
    return guildId;
}

static const char *vm_net_mock_role_spouse_name(const vm_net_mock_role_state *role)
{
    const char *envName;

    if (role != NULL && role->spouseRoleId != 0 && role->spouseName[0] != 0)
        return role->spouseName;
    envName = vm_net_mock_env_str("CBE_ACTOR_SPOUSE_NAME", NULL);
    if (envName != NULL && envName[0] != 0)
        return envName;
    return "\xce\xde"; /* GBK: 无 */
}

static u16 vm_net_mock_role_derived_attr(u32 level, u32 job, u32 attrIndex)
{
    static const u16 base[3][5] = {
        {12, 8, 7, 11, 3},
        {9, 14, 8, 8, 4},
        {7, 9, 15, 7, 5},
    };
    static const u16 gain[3][5] = {
        {3, 2, 1, 3, 1},
        {2, 3, 2, 2, 1},
        {1, 2, 4, 2, 1},
    };
    u32 jobIndex = (job == 0 || job > 3) ? 0 : job - 1;
    u32 value = 0;

    if (level == 0)
        level = 1;
    if (attrIndex >= 5)
        attrIndex = 0;
    value = base[jobIndex][attrIndex] + (level - 1) * gain[jobIndex][attrIndex];
    if (value > 65535)
        value = 65535;
    return (u16)value;
}

/* Server battle attack primary: 天机=力, 幻剑=敏, 鬼道=智. */
static u32 vm_net_mock_role_job_primary_attr(u32 job, u32 strength, u32 agility,
                                              u32 wisdom)
{
    if (job == 2)
        return agility;
    if (job == 3)
        return wisdom;
    return strength;
}

/*
 * Panel 魅力 (EXTRA132 → actor+0x84) is item-driven (flowers / 花), not
 * level/job growth or money. Until a persisted role charm + use-item path
 * exists, seed 0 so unequipped panels match 幻剑/鬼道 (and 天机 without 花).
 * CBE mid-session level-up still mutates +0x84 locally; next login reseeds.
 */
static u16 vm_net_mock_role_charm(const vm_net_mock_role_state *role, u32 level,
                                  u32 job)
{
    (void)role;
    (void)level;
    (void)job;
    return (u16)vm_net_mock_env_u32("CBE_ACTOR_CHARM", 0);
}

static u32 vm_net_mock_cap_u32(u32 value, u32 cap)
{
    return value > cap ? cap : value;
}

static void vm_net_mock_equipment_bonus_add(vm_net_mock_equipment_bonus *dst,
                                            const vm_net_mock_equipment_bonus *src)
{
    if (dst == NULL || src == NULL)
        return;
    dst->hp += src->hp;
    dst->mp += src->mp;
    dst->hpPercent += src->hpPercent;
    dst->mpPercent += src->mpPercent;
    dst->attack += src->attack;
    dst->armor += src->armor;
    dst->strength += src->strength;
    dst->agility += src->agility;
    dst->wisdom += src->wisdom;
    dst->crit += src->crit;
    dst->hit += src->hit;
    dst->dodge += src->dodge;
    dst->resist += src->resist;
}

/* equipment_bonus_scale / armor_bonus_scale / _enhance_delta /
 * equipment_bonus_add_unlocked_milestones: defined in mock_server_catalog.c */

static void vm_net_mock_role_collect_equipment_bonus(const vm_net_mock_role_state *role,
                                                     u32 level,
                                                     vm_net_mock_equipment_bonus *bonus)
{
    vm_net_mock_role_service_state *service = NULL;

    if (bonus == NULL)
        return;
    memset(bonus, 0, sizeof(*bonus));
    if (role == NULL)
        return;
    if (level == 0)
        level = 1;
    /* JianghuOL.CBE:0x0100FFEA � ldrsh item+0x110 (durability); cmp #0 / ble
     * skips the equip bonus when current durability is <= 0. */
    service = vm_net_mock_role_service_state_get(role);
    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        u32 itemId = role->equippedItemIds[slot];
        const vm_net_mock_equipment_catalog_item *item = NULL;
        vm_net_mock_equipment_bonus scaled;
        u16 enhanceLevel = 0;

        if (itemId == 0)
            continue;
        if (service != NULL &&
            service->equipmentItemIds[slot] == itemId &&
            service->durability[slot] == 0)
        {
            continue;
        }
        item = vm_net_mock_find_equipment_catalog_item(itemId);
        if (item == NULL || item->slot != slot)
            continue;
        if (item->levelRequired > level)
            continue;
        enhanceLevel = role->equippedEnhanceLevels[slot];
        if (enhanceLevel > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
            enhanceLevel = VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL;
        scaled = item->bonus;
        /* 物攻满量 M(L)；护甲强化增量减半（物防平衡）。 */
        scaled.attack =
            vm_net_mock_equipment_bonus_scale(scaled.attack, enhanceLevel);
        scaled.armor =
            vm_net_mock_equipment_armor_bonus_scale(scaled.armor, enhanceLevel);
        /* scaled.hp = vm_net_mock_equipment_bonus_scale(scaled.hp, enhanceLevel); */
        /* scaled.mp = vm_net_mock_equipment_bonus_scale(scaled.mp, enhanceLevel); */
        /* ??????????????????? add_unlocked_milestones? */
        vm_net_mock_equipment_bonus_add(bonus, &scaled);
        /*
         * ???? UI ????????????????unlock<=L ???
         * ???=Q+1 ????? bonus???%/??% ?????�????
         */
        vm_net_mock_equipment_bonus_add_unlocked_milestones(
            bonus, item, enhanceLevel, &role->equippedEnhanceWash[slot]);
    }
}

#include "mock_server_combat_formula_config.inc.c"

/* jobPrimary → panel/battle attack contribution percent (admin-configurable). */
static u32 vm_net_mock_role_job_primary_attack_coeff_percent(u32 job)
{
    const vm_net_mock_combat_formula_config *combatCfg =
        vm_net_mock_combat_formula_config_get();

    if (job == 2)
        return combatCfg ? combatCfg->primaryAttackCoeffHuanjianPercent : 100u;
    if (job == 3)
        return combatCfg ? combatCfg->primaryAttackCoeffGuidaoPercent : 100u;
    return combatCfg ? combatCfg->primaryAttackCoeffTianjiPercent : 100u;
}

static u32 vm_net_mock_role_primary_to_attack(u32 job, u32 primary)
{
    u32 coeff = vm_net_mock_role_job_primary_attack_coeff_percent(job);
    return (u32)(((uint64_t)primary * (uint64_t)coeff) / 100ull);
}

static int32_t vm_net_mock_role_primary_delta_to_attack(u32 job,
                                                       int32_t primaryDelta)
{
    u32 coeff = vm_net_mock_role_job_primary_attack_coeff_percent(job);
    return (int32_t)(((int64_t)primaryDelta * (int64_t)coeff) / 100);
}

static void vm_net_mock_role_build_player_stats(const vm_net_mock_role_state *role,
                                                vm_net_mock_player_stats *stats)
{
    u32 level = role ? role->level : 1;
    u32 job = role ? role->job : 1;
    vm_net_mock_equipment_bonus equipment;

    if (stats == NULL)
        return;
    memset(stats, 0, sizeof(*stats));
    if (level == 0 && role != NULL)
        level = vm_net_mock_role_level_from_exp(role->exp);
    if (level == 0)
        level = 1;
    if (job == 0 || job > 3)
        job = 1;

    vm_net_mock_role_collect_equipment_bonus(role, level, &equipment);
    stats->level = level;
    stats->job = job;
    stats->equipment = equipment;
    stats->baseStrength = vm_net_mock_role_derived_attr(level, job, 0);
    stats->baseAgility = vm_net_mock_role_derived_attr(level, job, 1);
    stats->baseWisdom = vm_net_mock_role_derived_attr(level, job, 2);
    stats->baseEndurance = vm_net_mock_role_derived_attr(level, job, 3);
    /* Charm is not a job growth column; see vm_net_mock_role_charm. */
    stats->baseCharm = vm_net_mock_role_charm(role, level, job);
    /*
     * Primary attrs feed skill coeff damage (??: wisdom�????).
     * Cap at 65535 to match client actor halfwords (u16).
     */
    stats->strength =
        vm_net_mock_cap_u32(stats->baseStrength + equipment.strength, 65535);
    stats->agility =
        vm_net_mock_cap_u32(stats->baseAgility + equipment.agility, 65535);
    stats->wisdom =
        vm_net_mock_cap_u32(stats->baseWisdom + equipment.wisdom, 65535);
    stats->endurance = vm_net_mock_cap_u32(stats->baseEndurance, 65535);
    stats->charm = vm_net_mock_cap_u32(stats->baseCharm, 65535);

    /*
     * HP/MP pools use job growth bases only.  Equipment 智慧 must not feed
     * maxMp (帽子 +16 智慧×3 曾把战斗/HUD 法力抬高数百).  Flat HP/MP
     * milestones stay absolute; 气血%/法力% use character bare pools
     * (same rule for both — not item dsh hp/mp columns).
     */
    {
        u32 bareHp = 90 + level * 8 + stats->endurance * 2;
        u32 bareMp = 70 + level * 9 + stats->baseWisdom * 3;
        u32 hpFromPct = 0;
        u32 mpFromPct = 0;

        if (equipment.hpPercent != 0)
            hpFromPct = (bareHp * equipment.hpPercent) / 100u;
        if (equipment.mpPercent != 0)
            mpFromPct = (bareMp * equipment.mpPercent) / 100u;
        /* Fold % into equipment.* so actorinfo bare = max - equipment still holds. */
        equipment.hp = vm_net_mock_cap_u32(equipment.hp + hpFromPct, 65535);
        equipment.mp = vm_net_mock_cap_u32(equipment.mp + mpFromPct, 65535);
        stats->equipment = equipment;
        stats->maxHp = bareHp + equipment.hp;
        stats->maxMp = bareMp + equipment.mp;
    }
    /*
     * Battle + panel overlay share this aggregate
     * (docs/re/2026-08-04-three-job-balance-pass1.md + admin primary coeffs):
     *   attack = jobPrimary * coeff% / 100 + equipment.attack
     * jobPrimary is 天机力 / 幻剑敏 / 鬼道智 (includes role base growth).
     * Client actor+0x130 still gets weapon FA via wear-apply; host overlay
     * realign uses this same stats->attack so panel matches battle.
     */
    stats->attack = vm_net_mock_cap_u32(
        vm_net_mock_role_primary_to_attack(
            job, vm_net_mock_role_job_primary_attr(job, stats->strength,
                                                   stats->agility,
                                                   stats->wisdom)) +
            equipment.attack,
        65535);
    /* Panel / actorinfo: bare (level/耐力) + full equip.armor.
     * Soft-curve NA mitigation uses equip.armor/divisor via
     * vm_net_mock_battle_mitigation_defense (admin soft_armor_k). */
    {
        const vm_net_mock_combat_formula_config *combatCfg =
            vm_net_mock_combat_formula_config_get();
        u32 defBase = combatCfg ? combatCfg->panelDefenseBase : 4u;
        u32 levelCoeff = combatCfg ? combatCfg->panelDefenseLevelCoeff : 1u;
        u32 endDiv = combatCfg ? combatCfg->panelDefenseEnduranceDiv : 2u;
        u32 hitBase = combatCfg ? combatCfg->panelHitBase : 101u;
        u32 endurancePart = 0;

        if (endDiv == 0)
            endDiv = 1;
        endurancePart = stats->endurance / endDiv;
        stats->defense = defBase + level * levelCoeff + endurancePart +
                         equipment.armor;
        stats->hit = vm_net_mock_add_capped_u32(hitBase, equipment.hit);
    }
    stats->dodge = equipment.dodge;
    stats->crit = equipment.crit;
    stats->resist = equipment.resist;

    stats->maxHp = vm_net_mock_cap_u32(stats->maxHp, 65535);
    stats->maxMp = vm_net_mock_cap_u32(stats->maxMp, 65535);
    stats->attack = vm_net_mock_cap_u32(stats->attack, 65535);
    stats->defense = vm_net_mock_cap_u32(stats->defense, 65535);
    stats->hit = vm_net_mock_cap_u32(stats->hit, 65535);
    stats->dodge = vm_net_mock_cap_u32(stats->dodge, 65535);
    stats->crit = vm_net_mock_cap_u32(stats->crit, 65535);
    stats->resist = vm_net_mock_cap_u32(stats->resist, 65535);
}

static u32 vm_net_mock_battle_apply_signed_stat_change(u32 value, int32_t change,
                                                         u32 max_value)
{
    if (max_value == 0)
        max_value = 65535u;
    if (change >= 0)
    {
        uint64_t raised = (uint64_t)value + (uint32_t)change;
        return raised > max_value ? max_value : (u32)raised;
    }
    {
        u32 reduction = (u32)(0 - change);
        return value > reduction ? value - reduction : 0;
    }
}

/* Timed spell modifiers live only for the active battle.  Applying them here
 * keeps normal role panels and durable base attributes untouched while the
 * existing battle formulas use the same derived-stat model as equipment. */
static void vm_net_mock_battle_apply_active_stat_modifier(vm_net_mock_player_stats *stats)
{
    const vm_net_mock_battle_stat_modifier *modifier =
        &g_vm_net_mock_battle_active_modifier_current;
    u32 primaryBefore = 0;
    u32 primaryAfter = 0;

    if (stats == NULL || modifier->remainingRounds == 0)
        return;
    primaryBefore = vm_net_mock_role_job_primary_attr(
        stats->job, stats->strength, stats->agility, stats->wisdom);
    stats->strength = vm_net_mock_battle_apply_signed_stat_change(
        stats->strength, modifier->strength, 65535u);
    stats->agility = vm_net_mock_battle_apply_signed_stat_change(
        stats->agility, modifier->agility, 65535u);
    stats->wisdom = vm_net_mock_battle_apply_signed_stat_change(
        stats->wisdom, modifier->wisdom, 65535u);
    /* Attack tracks scaled job-primary Δ (base+equip primary already in
     * strength/agility/wisdom). Hit/dodge/crit/resist stay equipment-only
     * except skill.dsh direct combat deltas. */
    primaryAfter = vm_net_mock_role_job_primary_attr(
        stats->job, stats->strength, stats->agility, stats->wisdom);
    stats->attack = vm_net_mock_battle_apply_signed_stat_change(
        stats->attack,
        vm_net_mock_role_primary_delta_to_attack(
            stats->job, (int32_t)primaryAfter - (int32_t)primaryBefore),
        65535u);
    stats->attack = vm_net_mock_battle_apply_signed_stat_change(
        stats->attack, modifier->attack, 65535u);
    stats->defense = vm_net_mock_battle_apply_signed_stat_change(
        stats->defense, modifier->defense, 65535u);
    stats->crit = vm_net_mock_battle_apply_signed_stat_change(
        stats->crit, modifier->crit, 65535u);
    stats->hit = vm_net_mock_battle_apply_signed_stat_change(
        stats->hit, modifier->hit, 65535u);
    stats->dodge = vm_net_mock_battle_apply_signed_stat_change(
        stats->dodge, modifier->dodge, 65535u);
    stats->resist = vm_net_mock_battle_apply_signed_stat_change(
        stats->resist, modifier->resist, 65535u);
}

static void vm_net_mock_role_sync_derived_vitals(vm_net_mock_role_state *role)
{
    vm_net_mock_player_stats stats;
    bool refillHp = false;
    bool refillMp = false;

    if (role == NULL)
        return;
    vm_net_mock_role_build_player_stats(role, &stats);
    refillHp = (role->hpMax == 0);
    refillMp = (role->mpMax == 0);
    role->hpMax = stats.maxHp ? stats.maxHp : VM_NET_MOCK_ROLE_DEFAULT_HP;
    role->mpMax = stats.maxMp ? stats.maxMp : VM_NET_MOCK_ROLE_DEFAULT_MP;
    if (refillHp)
        role->hp = role->hpMax;
    if (refillMp)
        role->mp = role->mpMax;
    if (role->hp > role->hpMax)
        role->hp = role->hpMax;
    if (role->mp > role->mpMax)
        role->mp = role->mpMax;
}

static bool vm_net_mock_role_add_exp(vm_net_mock_role_state *role, u32 addExp)
{
    u32 oldLevel = 1;
    u32 newLevel = 1;

    if (role == NULL || addExp == 0)
        return false;

    oldLevel = vm_net_mock_role_level_from_exp(role->exp);
    role->exp = vm_net_mock_add_capped_u32(role->exp, addExp);
    newLevel = vm_net_mock_role_level_from_exp(role->exp);
    role->level = newLevel;
    vm_net_mock_role_sync_derived_vitals(role);
    if (newLevel > oldLevel)
    {
        role->hp = role->hpMax;
        role->mp = role->mpMax;
        return true;
    }
    return false;
}

/*
 * Soft armor curve for normal attacks:
 *   damage = (attack * K + defense / 2) / (K + defense), K=2000
 * Floor 1.  Player panel defense stays full; callers pass mitigation defense
 * (bare + equip.armor/5) for role targets.  Monster catalog defense is used as-is.
 */
static u32 vm_net_mock_damage_after_defense(u32 attack, u32 defense)
{
    const vm_net_mock_combat_formula_config *combatCfg =
        vm_net_mock_combat_formula_config_get();
    u32 kSoft = combatCfg ? combatCfg->softArmorK : 2000u;
    uint64_t scaled = 0;

    if (kSoft == 0)
        kSoft = 1;
    if (attack == 0)
        attack = 1;
    scaled = ((uint64_t)attack * (uint64_t)kSoft + defense / 2u) /
             ((uint64_t)kSoft + (uint64_t)defense);
    if (scaled == 0)
        scaled = 1;
    if (scaled > 0xffffffffull)
        scaled = 0xffffffffull;
    return (u32)scaled;
}

/* Combat NA only: keep panel stats->defense full, scale equip armor by /div. */
static u32 vm_net_mock_battle_mitigation_defense(
    const vm_net_mock_player_stats *stats)
{
    const vm_net_mock_combat_formula_config *combatCfg =
        vm_net_mock_combat_formula_config_get();
    u32 bare = 0;
    u32 equipArmor = 0;
    u32 armorDiv = combatCfg ? combatCfg->equipArmorDivisor : 5u;

    if (stats == NULL)
        return 0;
    if (armorDiv == 0)
        armorDiv = 1;
    equipArmor = stats->equipment.armor;
    if (stats->defense > equipArmor)
        bare = stats->defense - equipArmor;
    else
        bare = stats->defense;
    return bare + equipArmor / armorDiv;
}

static void vm_net_mock_role_default_vitals(const vm_net_mock_role_state *role,
                                            u32 *hpOut, u32 *hpMaxOut,
                                            u32 *mpOut, u32 *mpMaxOut)
{
    vm_net_mock_player_stats stats;
    u32 hp = VM_NET_MOCK_ROLE_DEFAULT_HP;
    u32 mp = VM_NET_MOCK_ROLE_DEFAULT_MP;

    vm_net_mock_role_build_player_stats(role, &stats);
    if (role != NULL)
    {
        hp = role->hp;
        mp = role->mp;
    }
    if (stats.maxHp == 0)
        stats.maxHp = VM_NET_MOCK_ROLE_DEFAULT_HP;
    if (stats.maxMp == 0)
        stats.maxMp = VM_NET_MOCK_ROLE_DEFAULT_MP;
    if (hp > stats.maxHp)
        hp = stats.maxHp;
    if (mp > stats.maxMp)
        mp = stats.maxMp;
    if (hpOut)
        *hpOut = hp;
    if (hpMaxOut)
        *hpMaxOut = stats.maxHp;
    if (mpOut)
        *mpOut = mp;
    if (mpMaxOut)
        *mpMaxOut = stats.maxMp;
}

/*
 * PixiJS_web 人物信息：请求 1/1/21，响应同对象下发 build_player_stats
 * 全额汇总（基础 + 装备），与 CBE actorinfo 裸半字 / wear-apply 路径分开。
 * CBE 从不发 1/1/21，检测器可保持窄匹配。
 */
static bool vm_net_mock_is_pixi_panel_stats_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen &&
           object.major == 1 &&
           object.kind == 1 &&
           object.subtype == 21;
}

static u32 vm_net_mock_build_pixi_panel_stats_response(u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_player_stats stats;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 1;
    u32 hp = 0;
    u32 hpMax = 0;
    u32 mp = 0;
    u32 mpMax = 0;
    u32 expNext = 0;
    const char *title = "";
    const char *spouse = "";
    char guildName[VM_NET_MOCK_GUILD_NAME_SIZE];

    if (out == NULL || outCap < 16)
        return 0;
    if (role == NULL)
    {
        printf("[warn][network] mock_pixi_panel_stats reason=no-role\n");
        return 0;
    }

    vm_net_mock_role_sync_derived_vitals(role);
    vm_net_mock_role_build_player_stats(role, &stats);
    vm_net_mock_role_default_vitals(role, &hp, &hpMax, &mp, &mpMax);
    title = vm_net_mock_role_title(role);
    if (title == NULL)
        title = "";
    memset(guildName, 0, sizeof(guildName));
    vm_net_mock_role_guild_info(role, guildName, sizeof(guildName));
    spouse = vm_net_mock_role_spouse_name(role);
    if (spouse == NULL)
        spouse = "\xce\xde"; /* GBK: 无 */
    expNext = vm_net_mock_role_next_level_start_exp(role->exp);

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 1, 21, &objectStart) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "name",
                                       role->name[0] ? role->name : "") ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "title", title) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "spouse", spouse) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "spouseRoleId",
                                    role->spouseRoleId) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "guild", guildName) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "level", stats.level) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "job", stats.job) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "hp", hp) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "hpMax", stats.maxHp) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "mp", mp) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "mpMax", stats.maxMp) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "charm", stats.charm) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "strength", stats.strength) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "agility", stats.agility) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "wisdom", stats.wisdom) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "endurance", stats.endurance) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "attack", stats.attack) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "defense", stats.defense) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "hit", stats.hit) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "dodge", stats.dodge) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "crit", stats.crit) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "resist", stats.resist) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "baseStrength",
                                     stats.baseStrength) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "baseAgility",
                                     stats.baseAgility) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "baseWisdom",
                                     stats.baseWisdom) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "eqStrength",
                                     stats.equipment.strength) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "eqAgility",
                                     stats.equipment.agility) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "eqWisdom",
                                     stats.equipment.wisdom) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "eqAttack",
                                     stats.equipment.attack) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "eqArmor",
                                     stats.equipment.armor) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "exp", role->exp) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "expNext", expNext) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "money", role->money) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "coolmoney", role->wcoin) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "pkpoints", role->pkPoints) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "backpack",
                                     role->backpackCapacity))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_pixi_panel_stats role=%u level=%u "
           "str=%u/%u+%u atk=%u def=%u hit=%u dodge=%u crit=%u resist=%u "
           "hp=%u/%u mp=%u/%u evidence=build_player_stats+1/1/21\n",
           role->roleId, stats.level, stats.strength, stats.baseStrength,
           stats.equipment.strength, stats.attack, stats.defense, stats.hit,
           stats.dodge, stats.crit, stats.resist, hp, stats.maxHp, mp,
           stats.maxMp);
    return pos;
}

typedef enum
{
    VM_NET_MOCK_MONSTER_SLIME = 0,
    VM_NET_MOCK_MONSTER_BEAST,
    VM_NET_MOCK_MONSTER_FLYING,
    VM_NET_MOCK_MONSTER_INSECT,
    VM_NET_MOCK_MONSTER_REPTILE,
    VM_NET_MOCK_MONSTER_UNDEAD,
    VM_NET_MOCK_MONSTER_SPIRIT,
    VM_NET_MOCK_MONSTER_ELEMENTAL,
    VM_NET_MOCK_MONSTER_STONE,
    VM_NET_MOCK_MONSTER_HUMANOID,
    VM_NET_MOCK_MONSTER_SOLDIER,
    VM_NET_MOCK_MONSTER_BOSS
} vm_net_mock_monster_family;

#define VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX 0x7fffffffu
#define VM_NET_MOCK_MONSTER_DROP_MAX 8u
#define VM_NET_MOCK_MONSTER_CATALOG_MAX 192u
#define VM_NET_MOCK_MONSTER_CUSTOM_MAX 64u

typedef struct
{
    u16 enemyId;
    u8 level;
    u8 family;
    u32 dropItemId;
    u8 dropRatePercent;
} vm_net_mock_monster_entry;

typedef struct
{
    u32 itemId;
    u8 ratePercent;
} vm_net_mock_monster_drop;

typedef struct
{
    u32 enemyId;
    u32 level;
    u32 hp;
    u32 mp;
    u32 attack;
    u32 defense;
    u32 exp;
    u32 gold;
} vm_net_mock_monster_stats;

typedef struct
{
    bool used;
    u8 family;
    /* Explicit admin toggle: 1=counter may cast skills; 0=normal attack only.
     * Meaningful only when used==true; otherwise BOSS family is the default. */
    u8 castSkill;
    u8 dropCount;
    vm_net_mock_monster_stats stats;
    vm_net_mock_monster_drop drops[VM_NET_MOCK_MONSTER_DROP_MAX];
    /* Battle Actor key (e.g. e_boar.actor). Empty = fall back to SCE label. */
    char actorResource[64];
} vm_net_mock_monster_override;

typedef struct
{
    u32 enemyId;
    u32 level;
    u32 hp;
    u32 mp;
    u32 attack;
    u32 defense;
    u32 exp;
    u32 gold;
    u8 dropCount;
    vm_net_mock_monster_drop drops[VM_NET_MOCK_MONSTER_DROP_MAX];
    u8 family;
    u8 castSkill;
    bool overridden;
    bool custom;
    char displayName[32];
    char firstScene[64];
    char actorResource[64];
} vm_net_mock_monster_admin_row;

static const vm_net_mock_monster_entry g_vm_net_mock_monster_entries_builtin[] = {
    {  1,  6, VM_NET_MOCK_MONSTER_BEAST, 27, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    {  3,  1, VM_NET_MOCK_MONSTER_FLYING, 18, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    {  4,  2, VM_NET_MOCK_MONSTER_INSECT, 0, 0},
    {  6,  7, VM_NET_MOCK_MONSTER_BEAST, 29, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    {  9,  3, VM_NET_MOCK_MONSTER_BEAST, 25, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    /* SCE combat actor outside automonster.dsh: ???? (???). */
    { 12, 10, VM_NET_MOCK_MONSTER_BOSS, 0, 0},
    { 13, 12, VM_NET_MOCK_MONSTER_BOSS, 32, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 15, 55, VM_NET_MOCK_MONSTER_REPTILE, 34, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 18, 38, VM_NET_MOCK_MONSTER_ELEMENTAL, 36, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 19, 28, VM_NET_MOCK_MONSTER_BOSS, 37, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    /* SCE combat actor outside automonster.dsh: ???? (??). */
    { 21, 10, VM_NET_MOCK_MONSTER_ELEMENTAL, 0, 0},
    { 22, 12, VM_NET_MOCK_MONSTER_SPIRIT, 53, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    /* task.dsh type-2 kill target: ???????? (task 5004). */
    { 23, 40, VM_NET_MOCK_MONSTER_BOSS, 0, 0},
    /* SCE combat actor outside automonster.dsh: ????. */
    { 24, 28, VM_NET_MOCK_MONSTER_BOSS, 0, 0},
    { 25,  4, VM_NET_MOCK_MONSTER_UNDEAD, 43, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    /* SCE combat actor outside automonster.dsh: ????. */
    { 26, 12, VM_NET_MOCK_MONSTER_BOSS, 0, 0},
    { 28,  7, VM_NET_MOCK_MONSTER_FLYING, 45, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 29,  8, VM_NET_MOCK_MONSTER_FLYING, 0, 0},
    { 30,  8, VM_NET_MOCK_MONSTER_STONE, 47, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 31,  9, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
    { 32, 10, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    { 34, 11, VM_NET_MOCK_MONSTER_UNDEAD, 51, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 36, 14, VM_NET_MOCK_MONSTER_STONE, 52, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    /* task.dsh type-2 kill target: ??? (task 112). */
    { 38, 30, VM_NET_MOCK_MONSTER_FLYING, 0, 0},
    { 40, 20, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    { 41, 20, VM_NET_MOCK_MONSTER_SLIME, 55, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 42, 22, VM_NET_MOCK_MONSTER_ELEMENTAL, 56, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    /* task.dsh type-2 kill target: ??? (task 407). */
    { 44, 40, VM_NET_MOCK_MONSTER_REPTILE, 0, 0},
    { 45, 22, VM_NET_MOCK_MONSTER_SPIRIT, 58, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 47, 27, VM_NET_MOCK_MONSTER_HUMANOID, 63, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 48, 28, VM_NET_MOCK_MONSTER_SOLDIER, 0, 0},
    { 49, 31, VM_NET_MOCK_MONSTER_BEAST, 68, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 50, 30, VM_NET_MOCK_MONSTER_INSECT, 71, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 51, 31, VM_NET_MOCK_MONSTER_SPIRIT, 69, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 52, 28, VM_NET_MOCK_MONSTER_REPTILE, 0, 0},
    { 53, 29, VM_NET_MOCK_MONSTER_REPTILE, 67, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 54, 29, VM_NET_MOCK_MONSTER_REPTILE, 60, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 55, 34, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 56, 34, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 57, 32, VM_NET_MOCK_MONSTER_UNDEAD, 61, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 60, 36, VM_NET_MOCK_MONSTER_UNDEAD, 66, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 63, 60, VM_NET_MOCK_MONSTER_BOSS, 35, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 64, 37, VM_NET_MOCK_MONSTER_HUMANOID, 62, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 65, 60, VM_NET_MOCK_MONSTER_BOSS, 38, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 67, 27, VM_NET_MOCK_MONSTER_SOLDIER, 64, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 69, 24, VM_NET_MOCK_MONSTER_UNDEAD, 70, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 70, 24, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    { 71, 23, VM_NET_MOCK_MONSTER_STONE, 0, 0},
    /* SCE combat actor outside automonster.dsh: ????. */
    { 72,  8, VM_NET_MOCK_MONSTER_SOLDIER, 0, 0},
    { 73,  5, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 74,  5, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 75,  6, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 76,  3, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    { 77,  4, VM_NET_MOCK_MONSTER_UNDEAD, 28, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 78,  9, VM_NET_MOCK_MONSTER_UNDEAD, 50, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 79, 11, VM_NET_MOCK_MONSTER_STONE, 49, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    /* SCE combat actor outside automonster.dsh: ????. */
    { 80, 14, VM_NET_MOCK_MONSTER_STONE, 0, 0},
    { 81, 13, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    { 82, 15, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    { 83, 15, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    { 84, 17, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    /* SCE combat actor outside automonster.dsh: ???. */
    { 85, 17, VM_NET_MOCK_MONSTER_ELEMENTAL, 0, 0},
    { 86, 17, VM_NET_MOCK_MONSTER_SOLDIER, 0, 0},
    { 87, 17, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
    /* SCE combat actor outside automonster.dsh: ??? (????). */
    { 88, 18, VM_NET_MOCK_MONSTER_BOSS, 0, 0},
    { 89, 18, VM_NET_MOCK_MONSTER_SOLDIER, 0, 0},
    /* SCE combat actor outside automonster.dsh: ????. */
    { 90, 20, VM_NET_MOCK_MONSTER_BOSS, 0, 0},
    { 91, 21, VM_NET_MOCK_MONSTER_INSECT, 0, 0},
    { 92, 23, VM_NET_MOCK_MONSTER_SLIME, 57, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 94, 26, VM_NET_MOCK_MONSTER_INSECT, 59, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 97, 36, VM_NET_MOCK_MONSTER_ELEMENTAL, 65, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 98, 38, VM_NET_MOCK_MONSTER_SLIME, 0, 0},
    { 99, 39, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    /* SCE combat actor outside automonster.dsh: ???. */
    {100, 40, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    {101, 39, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
    /* SCE combat actor outside automonster.dsh: ???. */
    {102, 40, VM_NET_MOCK_MONSTER_STONE, 0, 0},
    {103, 40, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
    {104, 41, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
    {105,  1, VM_NET_MOCK_MONSTER_SLIME, VM_NET_MOCK_BATTLE_CHANGMING_SAN_ITEM_ID, VM_NET_MOCK_BATTLE_CHANGMING_SAN_DROP_RATE},
    {106,  2, VM_NET_MOCK_MONSTER_FLYING, 19, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    {107,  6, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    {110, 46, VM_NET_MOCK_MONSTER_STONE, 0, 0},
    {111, 48, VM_NET_MOCK_MONSTER_STONE, 0, 0},
    {112, 50, VM_NET_MOCK_MONSTER_STONE, 0, 0},
    {120, 50, VM_NET_MOCK_MONSTER_REPTILE, 0, 0},
    {121, 52, VM_NET_MOCK_MONSTER_ELEMENTAL, 0, 0},
    {122, 53, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    {200, 43, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    {201, 44, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    {202, 45, VM_NET_MOCK_MONSTER_UNDEAD, 80, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    {300, 55, VM_NET_MOCK_MONSTER_BOSS, 0, 0}
};

static vm_net_mock_monster_entry
    g_vm_net_mock_monster_entries[VM_NET_MOCK_MONSTER_CATALOG_MAX];
static bool g_vm_net_mock_monster_custom[VM_NET_MOCK_MONSTER_CATALOG_MAX];
static char g_vm_net_mock_monster_custom_names[VM_NET_MOCK_MONSTER_CATALOG_MAX][32];
static char g_vm_net_mock_monster_custom_sources[VM_NET_MOCK_MONSTER_CATALOG_MAX][64];
static char g_vm_net_mock_monster_custom_actors[VM_NET_MOCK_MONSTER_CATALOG_MAX][64];
static u32 g_vm_net_mock_monster_catalog_count = 0;
static bool g_vm_net_mock_monster_catalog_ready = false;
static vm_net_mock_monster_override
    g_vm_net_mock_monster_overrides[VM_NET_MOCK_MONSTER_CATALOG_MAX];
static bool g_vm_net_mock_monster_db_loaded = false;
static bool g_vm_net_mock_monster_db_valid = false;

static bool vm_net_mock_monster_db_load(void);

static u32 vm_net_mock_monster_builtin_count(void)
{
    return (u32)(sizeof(g_vm_net_mock_monster_entries_builtin) /
                 sizeof(g_vm_net_mock_monster_entries_builtin[0]));
}

static void vm_net_mock_monster_catalog_reset_builtin(void)
{
    u32 builtinCount = vm_net_mock_monster_builtin_count();

    memset(g_vm_net_mock_monster_entries, 0,
           sizeof(g_vm_net_mock_monster_entries));
    memset(g_vm_net_mock_monster_custom, 0,
           sizeof(g_vm_net_mock_monster_custom));
    memset(g_vm_net_mock_monster_custom_names, 0,
           sizeof(g_vm_net_mock_monster_custom_names));
    memset(g_vm_net_mock_monster_custom_sources, 0,
           sizeof(g_vm_net_mock_monster_custom_sources));
    memset(g_vm_net_mock_monster_custom_actors, 0,
           sizeof(g_vm_net_mock_monster_custom_actors));
    memset(g_vm_net_mock_monster_overrides, 0,
           sizeof(g_vm_net_mock_monster_overrides));
    if (builtinCount > VM_NET_MOCK_MONSTER_CATALOG_MAX)
        builtinCount = VM_NET_MOCK_MONSTER_CATALOG_MAX;
    memcpy(g_vm_net_mock_monster_entries, g_vm_net_mock_monster_entries_builtin,
           sizeof(g_vm_net_mock_monster_entries_builtin[0]) * builtinCount);
    g_vm_net_mock_monster_catalog_count = builtinCount;
    g_vm_net_mock_monster_catalog_ready = true;
}

static bool vm_net_mock_monster_enemy_id_known(u32 enemyId)
{
    if (enemyId == 0 || enemyId > 0xffffu)
        return false;
    (void)vm_net_mock_monster_db_load();
    for (u32 i = 0; i < g_vm_net_mock_monster_catalog_count; ++i)
    {
        if (g_vm_net_mock_monster_entries[i].enemyId == enemyId)
            return true;
    }
    return false;
}

static vm_net_mock_monster_entry vm_net_mock_monster_entry_for_enemy(u32 enemyId)
{
    vm_net_mock_monster_entry fallback;

    if (enemyId == 0)
        enemyId = VM_NET_MOCK_BATTLE_POISON_SLIME_ID;
    (void)vm_net_mock_monster_db_load();
    for (u32 i = 0; i < g_vm_net_mock_monster_catalog_count; ++i)
    {
        if (g_vm_net_mock_monster_entries[i].enemyId == enemyId)
            return g_vm_net_mock_monster_entries[i];
    }

    memset(&fallback, 0, sizeof(fallback));
    fallback.enemyId = (enemyId <= 0xffffu) ? (u16)enemyId : VM_NET_MOCK_BATTLE_POISON_SLIME_ID;
    fallback.family = VM_NET_MOCK_MONSTER_BEAST;
    if (enemyId >= 200)
        fallback.level = 45;
    else if (enemyId >= 120)
        fallback.level = 50;
    else if (enemyId >= 100)
        fallback.level = 30;
    else if (enemyId >= 70)
        fallback.level = 20;
    else if (enemyId >= 30)
        fallback.level = 10;
    else
        fallback.level = 3;
    return fallback;
}

static vm_net_mock_monster_stats vm_net_mock_monster_base_stats_for_enemy(u32 enemyId)
{
    vm_net_mock_monster_entry entry = vm_net_mock_monster_entry_for_enemy(enemyId);
    vm_net_mock_monster_stats stats;
    u32 level = entry.level ? entry.level : 1;

    memset(&stats, 0, sizeof(stats));
    stats.enemyId = entry.enemyId;
    stats.level = level;

    switch ((vm_net_mock_monster_family)entry.family)
    {
    case VM_NET_MOCK_MONSTER_SLIME:
        stats.hp = 16 + level * 4;
        stats.mp = 18 + level * 2;
        stats.attack = 6 + level * 2;
        stats.defense = 2 + level / 4;
        stats.exp = 3 + level * 2;
        stats.gold = 3 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_BEAST:
        stats.hp = 26 + level * 7;
        stats.mp = 10 + level;
        stats.attack = 7 + level * 2;
        stats.defense = 2 + level / 3;
        stats.exp = 4 + level * 3;
        stats.gold = 3 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_FLYING:
        stats.hp = 18 + level * 5;
        stats.mp = 12 + level;
        stats.attack = 8 + level * 2;
        stats.defense = 1 + level / 4;
        stats.exp = 4 + level * 3;
        stats.gold = 3 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_INSECT:
        stats.hp = 16 + level * 5;
        stats.mp = 12 + level;
        stats.attack = 8 + level * 2;
        stats.defense = 1 + level / 5;
        stats.exp = 4 + level * 3;
        stats.gold = 3 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_REPTILE:
        stats.hp = 24 + level * 6;
        stats.mp = 14 + level;
        stats.attack = 8 + level * 2;
        stats.defense = 2 + level / 3;
        stats.exp = 5 + level * 3;
        stats.gold = 4 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_UNDEAD:
        stats.hp = 34 + level * 8;
        stats.mp = 10 + level;
        stats.attack = 8 + level * 2;
        stats.defense = 4 + level / 3;
        stats.exp = 6 + level * 3;
        stats.gold = 4 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_SPIRIT:
        stats.hp = 22 + level * 6;
        stats.mp = 16 + level * 3;
        stats.attack = 10 + level * 2;
        stats.defense = 2 + level / 3;
        stats.exp = 6 + level * 3;
        stats.gold = 5 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_ELEMENTAL:
        stats.hp = 30 + level * 7;
        stats.mp = 20 + level * 4;
        stats.attack = 11 + level * 2;
        stats.defense = 3 + level / 3;
        stats.exp = 7 + level * 3;
        stats.gold = 5 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_STONE:
        stats.hp = 42 + level * 9;
        stats.mp = 12 + level * 2;
        stats.attack = 8 + level * 2;
        stats.defense = 6 + level / 2;
        stats.exp = 8 + level * 3;
        stats.gold = 5 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_HUMANOID:
        stats.hp = 30 + level * 7;
        stats.mp = 12 + level * 2;
        stats.attack = 9 + level * 2;
        stats.defense = 3 + level / 3;
        stats.exp = 6 + level * 3;
        stats.gold = 6 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_SOLDIER:
        stats.hp = 34 + level * 8;
        stats.mp = 10 + level;
        stats.attack = 10 + level * 2;
        stats.defense = 4 + level / 3;
        stats.exp = 7 + level * 3;
        stats.gold = 7 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_BOSS:
        stats.hp = 80 + level * 12;
        stats.mp = 24 + level * 4;
        stats.attack = 14 + level * 3;
        stats.defense = 8 + level / 2;
        stats.exp = 20 + level * 5;
        stats.gold = 25 + level * 4;
        break;
    default:
        stats.hp = 20 + level * 5;
        stats.mp = 10 + level;
        stats.attack = 7 + level * 2;
        stats.defense = 2 + level / 3;
        stats.exp = 4 + level * 3;
        stats.gold = 3 + level * 2;
        break;
    }

    if (stats.enemyId == VM_NET_MOCK_BATTLE_POISON_SLIME_ID)
    {
        stats.exp = VM_NET_MOCK_BATTLE_POISON_SLIME_EXP;
        stats.gold = VM_NET_MOCK_BATTLE_POISON_SLIME_GOLD;
    }
    if (stats.hp == 0)
        stats.hp = 1;
    if (stats.mp == 0)
        stats.mp = 1;
    if (stats.attack == 0)
        stats.attack = 1;
    if (stats.exp == 0)
        stats.exp = 1;
    return stats;
}

/*
 * Default combat-attribute scale by monster level.  Applied only to
 * hp/mp/attack/defense (not exp/gold).  Admin/MySQL store the unscaled base;
 * battle and scene seeding multiply at read time so edited values are not
 * double-scaled on save round-trips.
 *
 * Boss (family=BOSS) HP gets an extra fixed 5x after the level scale.
 */
static u32 vm_net_mock_monster_level_combat_attr_multiplier(u32 level)
{
    if (level >= 70u)
        return 15u;
    if (level >= 68u)
        return 10u;
    if (level >= 65u)
        return 9u;
    if (level >= 60u)
        return 8u;
     if (level >= 58u)
        return 7u;
    if (level >= 56u)
        return 6u;
    if (level >= 40u)
        return 5u;
    if (level >= 30u)
        return 4u;
    if (level >= 20u)
        return 3u;
    return 1u;
}

static u32 vm_net_mock_u32_mul_sat(u32 value, u32 multiplier)
{
    uint64_t product;

    if (multiplier <= 1u)
        return value;
    product = (uint64_t)value * (uint64_t)multiplier;
    return product > 0xffffffffull ? 0xffffffffu : (u32)product;
}

static void vm_net_mock_monster_stats_apply_combat_attr_multiplier(
    vm_net_mock_monster_stats *stats, u8 family)
{
    u32 multiplier;

    if (stats == NULL)
        return;
    multiplier = vm_net_mock_monster_level_combat_attr_multiplier(stats->level);
    if (multiplier > 1u)
    {
        if (multiplier >= 6u)
            stats->hp = vm_net_mock_u32_mul_sat(stats->hp, multiplier * 3u);
        else
            stats->hp = vm_net_mock_u32_mul_sat(stats->hp, multiplier);
        stats->mp = vm_net_mock_u32_mul_sat(stats->mp, multiplier);
        stats->attack = vm_net_mock_u32_mul_sat(stats->attack, multiplier);
        stats->defense = vm_net_mock_u32_mul_sat(stats->defense, multiplier);
    }
    if (family == (u8)VM_NET_MOCK_MONSTER_BOSS)
        stats->hp = vm_net_mock_u32_mul_sat(stats->hp, 5u);
    if (stats->hp == 0)
        stats->hp = 1;
    if (stats->mp == 0)
        stats->mp = 1;
    if (stats->attack == 0)
        stats->attack = 1;
}

static int vm_net_mock_monster_catalog_index(u32 enemyId)
{
    if (!g_vm_net_mock_monster_catalog_ready)
        vm_net_mock_monster_catalog_reset_builtin();
    for (u32 i = 0; i < g_vm_net_mock_monster_catalog_count; ++i)
    {
        if (g_vm_net_mock_monster_entries[i].enemyId == enemyId)
            return (int)i;
    }
    return -1;
}

static bool vm_net_mock_monster_catalog_is_custom(u32 enemyId)
{
    int index;

    (void)vm_net_mock_monster_db_load();
    index = vm_net_mock_monster_catalog_index(enemyId);
    return index >= 0 && g_vm_net_mock_monster_custom[index];
}

typedef struct
{
    u32 loaded;
    u32 skipped;
    u32 dropsLoaded;
    u32 dropsSkipped;
} vm_net_mock_monster_db_load_context;

typedef struct
{
    u32 count;
    bool found;
    bool invalid;
} vm_net_mock_monster_column_context;

static bool vm_net_mock_monster_column_count_row(void *contextValue,
                                                 unsigned int columnCount,
                                                 const char *const *values,
                                                 const size_t *lengths)
{
    vm_net_mock_monster_column_context *context =
        (vm_net_mock_monster_column_context *)contextValue;

    if (context == NULL || context->found || columnCount != 1 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->count))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

/* Existing DBs created before cast_skill need an additive migrate; new CREATE
 * already embeds the column.  Backfill: former BOSS family rows default on. */
static bool vm_net_mock_monster_ensure_cast_skill_column(void)
{
    vm_net_mock_monster_column_context context;
    char query[384];

    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(
            "SELECT COUNT(*) FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='server_monsters' "
            "AND COLUMN_NAME='cast_skill'",
            vm_net_mock_monster_column_count_row, &context) ||
        context.invalid || !context.found)
    {
        printf("[error][mock-admin] monster_cast_skill_column probe error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    if (context.count != 0)
        return true;
    if (!vm_mysql_exec(
            "ALTER TABLE server_monsters "
            "ADD COLUMN cast_skill TINYINT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER family"))
    {
        printf("[error][mock-admin] monster_cast_skill_column alter error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    snprintf(query, sizeof(query),
             "UPDATE server_monsters SET cast_skill=1 WHERE family=%u",
             (u32)VM_NET_MOCK_MONSTER_BOSS);
    if (!vm_mysql_exec(query))
    {
        printf("[error][mock-admin] monster_cast_skill_column backfill error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    printf("[info][mock-admin] monster_cast_skill_column added "
           "evidence=admin-per-monster-boss-skill\n");
    return true;
}

/* Existing DBs created before actor_resource need an additive migrate. */
static bool vm_net_mock_monster_ensure_actor_resource_column(void)
{
    vm_net_mock_monster_column_context context;

    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(
            "SELECT COUNT(*) FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='server_monsters' "
            "AND COLUMN_NAME='actor_resource'",
            vm_net_mock_monster_column_count_row, &context) ||
        context.invalid || !context.found)
    {
        printf("[error][mock-admin] monster_actor_resource_column probe error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    if (context.count == 0)
    {
        if (!vm_mysql_exec(
                "ALTER TABLE server_monsters "
                "ADD COLUMN actor_resource VARBINARY(63) NOT NULL DEFAULT '' "
                "AFTER cast_skill"))
        {
            printf("[error][mock-admin] monster_actor_resource_column alter "
                   "error=%s\n",
                   vm_mysql_last_error());
            return false;
        }
        printf("[info][mock-admin] monster_actor_resource_column added "
               "table=server_monsters\n");
    }

    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(
            "SELECT COUNT(*) FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA=DATABASE() AND "
            "TABLE_NAME='server_monster_catalog_extra' "
            "AND COLUMN_NAME='actor_resource'",
            vm_net_mock_monster_column_count_row, &context) ||
        context.invalid || !context.found)
    {
        printf("[error][mock-admin] monster_catalog_actor_column probe error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    if (context.count != 0)
        return true;
    if (!vm_mysql_exec(
            "ALTER TABLE server_monster_catalog_extra "
            "ADD COLUMN actor_resource VARBINARY(63) NOT NULL DEFAULT '' "
            "AFTER source_label"))
    {
        printf("[error][mock-admin] monster_catalog_actor_column alter "
               "error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    printf("[info][mock-admin] monster_actor_resource_column added "
           "table=server_monster_catalog_extra\n");
    return true;
}

static bool vm_net_mock_monster_db_row(void *contextValue,
                                       unsigned int columnCount,
                                       const char *const *values,
                                       const size_t *lengths)
{
    vm_net_mock_monster_db_load_context *context =
        (vm_net_mock_monster_db_load_context *)contextValue;
    u32 number[10];
    char actorResource[64];
    size_t actorLen = 0;
    int index = -1;
    vm_net_mock_monster_override *override = NULL;

    memset(number, 0, sizeof(number));
    memset(actorResource, 0, sizeof(actorResource));
    if (context == NULL || columnCount != 11)
        return false;
    for (u32 i = 0; i < 10; ++i)
    {
        if (!vm_mock_mysql_parse_u32(values[i], lengths[i], &number[i]))
        {
            ++context->skipped;
            return true;
        }
    }
    if (values[10] != NULL && lengths[10] != 0 &&
        !vm_mysql_hex_decode(values[10], lengths[10], actorResource,
                             sizeof(actorResource) - 1, &actorLen))
    {
        ++context->skipped;
        return true;
    }
    actorResource[actorLen] = 0;
    if (actorResource[0] != 0 &&
        (!vm_net_mock_str_ends_with(actorResource, ".actor") ||
         strlen(actorResource) >= sizeof(actorResource)))
    {
        ++context->skipped;
        return true;
    }
    index = vm_net_mock_monster_catalog_index(number[0]);
    if (index < 0 || number[1] == 0 || number[1] > 0xffu ||
        number[2] > VM_NET_MOCK_MONSTER_BOSS || number[3] == 0 ||
        number[4] == 0 || number[5] == 0 ||
        number[3] > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        number[4] > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        number[5] > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        number[6] > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        number[7] > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        number[8] > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        number[9] > 1u)
    {
        ++context->skipped;
        return true;
    }

    override = &g_vm_net_mock_monster_overrides[index];
    memset(override, 0, sizeof(*override));
    override->used = true;
    override->family = (u8)number[2];
    override->stats.enemyId = number[0];
    override->stats.level = number[1];
    override->stats.hp = number[3];
    override->stats.mp = number[4];
    override->stats.attack = number[5];
    override->stats.defense = number[6];
    override->stats.exp = number[7];
    override->stats.gold = number[8];
    override->castSkill = (u8)number[9];
    if (actorResource[0] != 0)
    {
        snprintf(override->actorResource, sizeof(override->actorResource), "%s",
                 actorResource);
    }
    ++context->loaded;
    return true;
}

static bool vm_net_mock_monster_db_drop_row(void *contextValue,
                                            unsigned int columnCount,
                                            const char *const *values,
                                            const size_t *lengths)
{
    vm_net_mock_monster_db_load_context *context =
        (vm_net_mock_monster_db_load_context *)contextValue;
    u32 number[4];
    int index = -1;
    vm_net_mock_monster_override *override = NULL;

    memset(number, 0, sizeof(number));
    if (context == NULL || columnCount != 4)
        return false;
    for (u32 i = 0; i < 4; ++i)
    {
        if (!vm_mock_mysql_parse_u32(values[i], lengths[i], &number[i]))
        {
            ++context->dropsSkipped;
            return true;
        }
    }
    index = vm_net_mock_monster_catalog_index(number[0]);
    if (index < 0 || number[1] == 0 ||
        number[1] > VM_NET_MOCK_MONSTER_DROP_MAX || number[2] == 0 ||
        number[3] == 0 || number[3] > 100u ||
        !vm_net_mock_shop_catalog_has_loaded_item(number[2]))
    {
        ++context->dropsSkipped;
        return true;
    }
    override = &g_vm_net_mock_monster_overrides[index];
    if (!override->used ||
        override->dropCount >= VM_NET_MOCK_MONSTER_DROP_MAX)
    {
        ++context->dropsSkipped;
        return true;
    }
    override->drops[override->dropCount].itemId = number[2];
    override->drops[override->dropCount].ratePercent = (u8)number[3];
    ++override->dropCount;
    ++context->dropsLoaded;
    return true;
}

static bool vm_net_mock_monster_catalog_extra_row(void *contextValue,
                                                  unsigned int columnCount,
                                                  const char *const *values,
                                                  const size_t *lengths)
{
    vm_net_mock_monster_db_load_context *context =
        (vm_net_mock_monster_db_load_context *)contextValue;
    u32 monsterId = 0;
    u32 level = 0;
    u32 family = 0;
    char displayName[32];
    char sourceLabel[64];
    char actorResource[64];
    size_t nameLen = 0;
    size_t sourceLen = 0;
    size_t actorLen = 0;
    u32 slot = 0;

    memset(displayName, 0, sizeof(displayName));
    memset(sourceLabel, 0, sizeof(sourceLabel));
    memset(actorResource, 0, sizeof(actorResource));
    if (context == NULL || columnCount != 6)
        return false;
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &monsterId) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &level) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &family) ||
        monsterId == 0 || monsterId > 0xffffu || level == 0 || level > 0xffu ||
        family > VM_NET_MOCK_MONSTER_BOSS ||
        !vm_mysql_hex_decode(values[3], lengths[3], displayName,
                             sizeof(displayName) - 1, &nameLen) ||
        nameLen == 0 ||
        !vm_mysql_hex_decode(values[4], lengths[4], sourceLabel,
                             sizeof(sourceLabel) - 1, &sourceLen))
    {
        ++context->skipped;
        return true;
    }
    if (values[5] != NULL && lengths[5] != 0 &&
        !vm_mysql_hex_decode(values[5], lengths[5], actorResource,
                             sizeof(actorResource) - 1, &actorLen))
    {
        ++context->skipped;
        return true;
    }
    displayName[nameLen] = 0;
    sourceLabel[sourceLen] = 0;
    actorResource[actorLen] = 0;
    if (actorResource[0] != 0 &&
        !vm_net_mock_str_ends_with(actorResource, ".actor"))
    {
        ++context->skipped;
        return true;
    }
    if (vm_net_mock_monster_catalog_index(monsterId) >= 0)
    {
        ++context->skipped;
        return true;
    }
    if (g_vm_net_mock_monster_catalog_count >= VM_NET_MOCK_MONSTER_CATALOG_MAX)
    {
        ++context->skipped;
        return true;
    }
    slot = g_vm_net_mock_monster_catalog_count++;
    memset(&g_vm_net_mock_monster_entries[slot], 0,
           sizeof(g_vm_net_mock_monster_entries[slot]));
    g_vm_net_mock_monster_entries[slot].enemyId = (u16)monsterId;
    g_vm_net_mock_monster_entries[slot].level = (u8)level;
    g_vm_net_mock_monster_entries[slot].family = (u8)family;
    g_vm_net_mock_monster_custom[slot] = true;
    snprintf(g_vm_net_mock_monster_custom_names[slot],
             sizeof(g_vm_net_mock_monster_custom_names[slot]), "%s",
             displayName);
    snprintf(g_vm_net_mock_monster_custom_sources[slot],
             sizeof(g_vm_net_mock_monster_custom_sources[slot]), "%s",
             sourceLabel[0] != 0 ? sourceLabel : "?????");
    if (actorResource[0] != 0)
    {
        snprintf(g_vm_net_mock_monster_custom_actors[slot],
                 sizeof(g_vm_net_mock_monster_custom_actors[slot]), "%s",
                 actorResource);
    }
    ++context->loaded;
    return true;
}

static bool vm_net_mock_monster_db_load_unlocked(void)
{
    vm_net_mock_monster_db_load_context context;
    vm_net_mock_monster_db_load_context catalogContext;

    if (g_vm_net_mock_monster_db_loaded)
        return g_vm_net_mock_monster_db_valid;
    g_vm_net_mock_monster_db_loaded = true;
    g_vm_net_mock_monster_db_valid = false;
    vm_net_mock_monster_catalog_reset_builtin();
    memset(&context, 0, sizeof(context));
    memset(&catalogContext, 0, sizeof(catalogContext));

    /*
     * Drop rows validate item ids against the shop catalog.  Materialize the
     * DSH(+admin) catalog before opening the drops result set so the row
     * callback never issues a nested MySQL query on this connection.
     */
    (void)vm_net_mock_load_shop_catalog();

    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_monster_catalog_extra ("
            "monster_id SMALLINT UNSIGNED NOT NULL,"
            "level TINYINT UNSIGNED NOT NULL,"
            "family TINYINT UNSIGNED NOT NULL,"
            "display_name VARBINARY(31) NOT NULL,"
            "source_label VARBINARY(63) NOT NULL,"
            "actor_resource VARBINARY(63) NOT NULL DEFAULT '',"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(monster_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_monsters ("
            "monster_id SMALLINT UNSIGNED NOT NULL,level TINYINT UNSIGNED NOT NULL,"
            "family TINYINT UNSIGNED NOT NULL,"
            "cast_skill TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "actor_resource VARBINARY(63) NOT NULL DEFAULT '',"
            "hp INT UNSIGNED NOT NULL,"
            "mp INT UNSIGNED NOT NULL,attack_value INT UNSIGNED NOT NULL,"
            "defense_value INT UNSIGNED NOT NULL,reward_exp INT UNSIGNED NOT NULL,"
            "reward_money INT UNSIGNED NOT NULL,drop_item_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "drop_rate_percent TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(monster_id)) ENGINE=InnoDB") ||
        !vm_net_mock_monster_ensure_cast_skill_column() ||
        !vm_net_mock_monster_ensure_actor_resource_column() ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_monster_drops ("
            "monster_id SMALLINT UNSIGNED NOT NULL,"
            "drop_slot TINYINT UNSIGNED NOT NULL,"
            "item_id INT UNSIGNED NOT NULL,"
            "drop_rate_percent TINYINT UNSIGNED NOT NULL,"
            "PRIMARY KEY(monster_id,drop_slot),"
            "CONSTRAINT fk_server_monster_drops_monster "
            "FOREIGN KEY(monster_id) REFERENCES server_monsters(monster_id) "
            "ON DELETE CASCADE) ENGINE=InnoDB") ||
        /* Old single-drop rows are a one-way compatibility source.  New
         * saves clear those legacy columns, so a later zero-drop edit cannot
         * be silently recreated on the next service restart. */
        !vm_mysql_exec(
            "INSERT IGNORE INTO server_monster_drops("
            "monster_id,drop_slot,item_id,drop_rate_percent) "
            "SELECT monster_id,1,drop_item_id,drop_rate_percent "
            "FROM server_monsters WHERE drop_item_id<>0 AND drop_rate_percent<>0") ||
        !vm_mysql_query(
            "SELECT monster_id,level,family,HEX(display_name),HEX(source_label),"
            "HEX(actor_resource) "
            "FROM server_monster_catalog_extra ORDER BY monster_id",
            vm_net_mock_monster_catalog_extra_row, &catalogContext) ||
        !vm_mysql_query(
            "SELECT monster_id,level,family,hp,mp,attack_value,defense_value,"
            "reward_exp,reward_money,cast_skill,HEX(actor_resource) "
            "FROM server_monsters ORDER BY monster_id",
            vm_net_mock_monster_db_row, &context) ||
        !vm_mysql_query(
            "SELECT monster_id,drop_slot,item_id,drop_rate_percent "
            "FROM server_monster_drops ORDER BY monster_id,drop_slot",
            vm_net_mock_monster_db_drop_row, &context))
    {
        printf("[error][mock-admin] monster_db_load failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_monster_db_valid = true;
    printf("[info][mock-admin] monster_db_load custom=%u custom_skipped=%u "
           "rows=%u skipped=%u drops=%u drops_skipped=%u catalog=%u\n",
           catalogContext.loaded, catalogContext.skipped, context.loaded,
           context.skipped, context.dropsLoaded, context.dropsSkipped,
           g_vm_net_mock_monster_catalog_count);
    return true;
}

/* Serialize catalog materialization so admin public GETs can load without
 * holding g_vm_mock_service_protocol_mutex (avoids protocol_lock convoy). */
static bool vm_net_mock_monster_db_load(void)
{
    static pthread_mutex_t s_load_mu = PTHREAD_MUTEX_INITIALIZER;
    bool ok;

    pthread_mutex_lock(&s_load_mu);
    ok = vm_net_mock_monster_db_load_unlocked();
    pthread_mutex_unlock(&s_load_mu);
    return ok;
}

static vm_net_mock_monster_stats vm_net_mock_monster_stats_for_enemy_raw(u32 enemyId)
{
    int index;

    /* Load MySQL catalog_extra + overrides BEFORE indexing.  Custom ids such as
     * 203/204 only exist after db_load; indexing first left index=-1 and battle
     * fell back to the family formula (e.g. spirit Lv43 → 1400 HP) while admin
     * still showed the saved override. */
    (void)vm_net_mock_monster_db_load();
    index = vm_net_mock_monster_catalog_index(enemyId);
    if (index >= 0 && g_vm_net_mock_monster_overrides[index].used)
        return g_vm_net_mock_monster_overrides[index].stats;
    return vm_net_mock_monster_base_stats_for_enemy(enemyId);
}

static u8 vm_net_mock_monster_family_for_enemy(u32 enemyId)
{
    int index;

    (void)vm_net_mock_monster_db_load();
    index = vm_net_mock_monster_catalog_index(enemyId);
    if (index >= 0 && g_vm_net_mock_monster_overrides[index].used)
        return g_vm_net_mock_monster_overrides[index].family;
    return vm_net_mock_monster_entry_for_enemy(enemyId).family;
}

/* Counter-turn skill casting: MySQL override owns an explicit cast_skill bit;
 * without override, BOSS family remains the built-in default.  Global
 * CBE_BATTLE_BOSS_SKILL=0 still disables all casts in the battle resolver. */
static bool vm_net_mock_monster_casts_active_skill(u32 enemyId)
{
    int index;

    (void)vm_net_mock_monster_db_load();
    index = vm_net_mock_monster_catalog_index(enemyId);
    if (index >= 0 && g_vm_net_mock_monster_overrides[index].used)
        return g_vm_net_mock_monster_overrides[index].castSkill != 0;
    return vm_net_mock_monster_entry_for_enemy(enemyId).family ==
           (u8)VM_NET_MOCK_MONSTER_BOSS;
}

static vm_net_mock_monster_stats vm_net_mock_monster_stats_for_enemy(u32 enemyId)
{
    vm_net_mock_monster_stats stats;

    /*
     * Admin/MySQL and builtin formula both store BASE hp/mp/atk/def.
     * Battle / scene seeding always apply level (+ boss HP) multipliers here.
     * Skipping the scale for override rows made configured monsters much
     * weaker than the same base under the formula path ("属性变少了").
     */
    stats = vm_net_mock_monster_stats_for_enemy_raw(enemyId);
    vm_net_mock_monster_stats_apply_combat_attr_multiplier(
        &stats, vm_net_mock_monster_family_for_enemy(enemyId));
    return stats;
}

/* The source catalog predates backend editing and stores one legacy drop per
 * monster.  MySQL overrides deliberately own the complete list, including an
 * explicitly empty list, so an administrator can remove a built-in material
 * drop without it reappearing after reload. */
static u8 vm_net_mock_monster_drops_for_enemy(
    u32 enemyId, vm_net_mock_monster_drop *drops, u8 dropCap)
{
    int index;
    vm_net_mock_monster_entry entry;
    u8 total = 0;

    (void)vm_net_mock_monster_db_load();
    index = vm_net_mock_monster_catalog_index(enemyId);
    entry = vm_net_mock_monster_entry_for_enemy(enemyId);
    if (index >= 0 && g_vm_net_mock_monster_overrides[index].used)
    {
        total = g_vm_net_mock_monster_overrides[index].dropCount;
        if (total > VM_NET_MOCK_MONSTER_DROP_MAX)
            total = VM_NET_MOCK_MONSTER_DROP_MAX;
        if (drops != NULL && dropCap != 0)
        {
            u8 copied = total < dropCap ? total : dropCap;
            memcpy(drops, g_vm_net_mock_monster_overrides[index].drops,
                   sizeof(*drops) * copied);
        }
        return total;
    }
    if (entry.dropItemId != 0 && entry.dropRatePercent != 0)
    {
        total = 1;
        if (drops != NULL && dropCap != 0)
        {
            drops[0].itemId = entry.dropItemId;
            drops[0].ratePercent = entry.dropRatePercent;
        }
    }
    return total;
}

static bool vm_net_mock_monster_admin_save(
    const vm_net_mock_monster_admin_row *row, const char **errorOut)
{
    char query[1280];
    char mysqlError[512];
    char actorHex[160];
    int index = -1;
    vm_net_mock_monster_override *override = NULL;
    bool transactionStarted = false;

    if (errorOut)
        *errorOut = "??????";
    if (row == NULL || row->enemyId == 0 || row->level == 0 ||
        row->level > 0xffu || row->family > VM_NET_MOCK_MONSTER_BOSS ||
        row->hp == 0 || row->mp == 0 || row->attack == 0 ||
        row->hp > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->mp > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->attack > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->defense > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->exp > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->gold > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->dropCount > VM_NET_MOCK_MONSTER_DROP_MAX)
    {
        return false;
    }
    if (row->actorResource[0] != 0 &&
        (!vm_net_mock_str_ends_with(row->actorResource, ".actor") ||
         strlen(row->actorResource) >= sizeof(row->actorResource)))
    {
        if (errorOut)
            *errorOut = "?? Actor ?????";
        return false;
    }
    index = vm_net_mock_monster_catalog_index(row->enemyId);
    if (index < 0)
    {
        if (errorOut)
            *errorOut = "????????? ID";
        return false;
    }
    for (u8 i = 0; i < row->dropCount; ++i)
    {
        if (row->drops[i].itemId == 0 || row->drops[i].ratePercent == 0 ||
            row->drops[i].ratePercent > 100u ||
            vm_net_mock_find_shop_catalog_item(row->drops[i].itemId) == NULL)
        {
            if (errorOut)
                *errorOut = "???? ID ?????";
            return false;
        }
        for (u8 previous = 0; previous < i; ++previous)
        {
            if (row->drops[previous].itemId == row->drops[i].itemId)
            {
                if (errorOut)
                    *errorOut = "????????????????";
                return false;
            }
        }
    }
    if (!g_vm_net_mock_monster_db_valid)
    {
        g_vm_net_mock_monster_db_loaded = false;
        if (!vm_net_mock_monster_db_load())
        {
            if (errorOut)
                *errorOut = vm_mysql_last_error();
            return false;
        }
    }

    memset(actorHex, 0, sizeof(actorHex));
    if (row->actorResource[0] != 0 &&
        vm_mysql_hex_encode(row->actorResource, strlen(row->actorResource),
                            actorHex, sizeof(actorHex)) == 0)
    {
        if (errorOut)
            *errorOut = "?? Actor ????";
        return false;
    }

    snprintf(
        query, sizeof(query),
        "INSERT INTO server_monsters(monster_id,level,family,cast_skill,actor_resource,"
        "hp,mp,attack_value,defense_value,reward_exp,reward_money,drop_item_id,"
        "drop_rate_percent) "
        "VALUES(%u,%u,%u,%u,UNHEX('%s'),%u,%u,%u,%u,%u,%u,0,0) ON DUPLICATE KEY UPDATE "
        "level=VALUES(level),family=VALUES(family),cast_skill=VALUES(cast_skill),"
        "actor_resource=VALUES(actor_resource),"
        "hp=VALUES(hp),mp=VALUES(mp),"
        "attack_value=VALUES(attack_value),defense_value=VALUES(defense_value),"
        "reward_exp=VALUES(reward_exp),reward_money=VALUES(reward_money),"
        "drop_item_id=0,drop_rate_percent=0",
        row->enemyId, row->level, row->family, row->castSkill ? 1u : 0u, actorHex,
        row->hp, row->mp, row->attack, row->defense, row->exp, row->gold);
    if (!vm_mysql_exec("START TRANSACTION"))
        goto mysql_failed;
    transactionStarted = true;
    if (!vm_mysql_exec(query))
        goto mysql_failed;
    if (g_vm_net_mock_monster_custom[index])
    {
        snprintf(query, sizeof(query),
                 "UPDATE server_monster_catalog_extra SET "
                 "actor_resource=UNHEX('%s') WHERE monster_id=%u",
                 actorHex, row->enemyId);
        if (!vm_mysql_exec(query))
            goto mysql_failed;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM server_monster_drops WHERE monster_id=%u",
             row->enemyId);
    if (!vm_mysql_exec(query))
        goto mysql_failed;
    for (u8 i = 0; i < row->dropCount; ++i)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO server_monster_drops("
                 "monster_id,drop_slot,item_id,drop_rate_percent) "
                 "VALUES(%u,%u,%u,%u)",
                 row->enemyId, (u32)i + 1u, row->drops[i].itemId,
                 row->drops[i].ratePercent);
        if (!vm_mysql_exec(query))
            goto mysql_failed;
    }
    if (!vm_mysql_exec("COMMIT"))
        goto mysql_failed;
    transactionStarted = false;

    override = &g_vm_net_mock_monster_overrides[index];
    memset(override, 0, sizeof(*override));
    override->used = true;
    override->family = (u8)row->family;
    override->castSkill = row->castSkill ? 1u : 0u;
    override->stats.enemyId = row->enemyId;
    override->stats.level = row->level;
    override->stats.hp = row->hp;
    override->stats.mp = row->mp;
    override->stats.attack = row->attack;
    override->stats.defense = row->defense;
    override->stats.exp = row->exp;
    override->stats.gold = row->gold;
    if (row->actorResource[0] != 0)
    {
        snprintf(override->actorResource, sizeof(override->actorResource), "%s",
                 row->actorResource);
    }
    if (g_vm_net_mock_monster_custom[index])
    {
        snprintf(g_vm_net_mock_monster_custom_actors[index],
                 sizeof(g_vm_net_mock_monster_custom_actors[index]), "%s",
                 row->actorResource);
    }
    override->dropCount = row->dropCount;
    if (row->dropCount != 0)
    {
        memcpy(override->drops, row->drops,
               sizeof(override->drops[0]) * row->dropCount);
    }
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] monster_save id=%u level=%u family=%u cast_skill=%u "
           "actor=%s hp=%u mp=%u attack=%u defense=%u exp=%u money=%u drops=%u\n",
           row->enemyId, row->level, row->family, row->castSkill ? 1u : 0u,
           row->actorResource[0] != 0 ? row->actorResource : "-",
           row->hp, row->mp, row->attack, row->defense, row->exp, row->gold,
           row->dropCount);
    return true;

mysql_failed:
    snprintf(mysqlError, sizeof(mysqlError), "%s", vm_mysql_last_error());
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    printf("[error][mock-admin] monster_save_failed id=%u drops=%u error=%s\n",
           row->enemyId, row->dropCount, mysqlError);
    if (errorOut)
        *errorOut = "??????????????? MySQL ??";
    return false;
}

static void vm_net_mock_monster_resource_labels_invalidate(void);

static bool vm_net_mock_monster_admin_reset(u32 enemyId,
                                            const char **errorOut)
{
    char query[256];
    char mysqlError[512];
    int index = vm_net_mock_monster_catalog_index(enemyId);
    bool transactionStarted = false;

    if (errorOut)
        *errorOut = "????????? ID";
    if (index < 0)
        return false;
    if (g_vm_net_mock_monster_custom[index])
    {
        if (errorOut)
            *errorOut = "??????????????????";
        return false;
    }
    if (!g_vm_net_mock_monster_db_valid)
    {
        g_vm_net_mock_monster_db_loaded = false;
        if (!vm_net_mock_monster_db_load())
        {
            if (errorOut)
                *errorOut = vm_mysql_last_error();
            return false;
        }
    }
    if (!vm_mysql_exec("START TRANSACTION"))
        goto mysql_failed;
    transactionStarted = true;
    snprintf(query, sizeof(query),
             "DELETE FROM server_monster_drops WHERE monster_id=%u", enemyId);
    if (!vm_mysql_exec(query))
        goto mysql_failed;
    snprintf(query, sizeof(query),
             "DELETE FROM server_monsters WHERE monster_id=%u", enemyId);
    if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        goto mysql_failed;
    transactionStarted = false;
    memset(&g_vm_net_mock_monster_overrides[index], 0,
           sizeof(g_vm_net_mock_monster_overrides[index]));
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] monster_reset id=%u source=server-default\n",
           enemyId);
    return true;

mysql_failed:
    snprintf(mysqlError, sizeof(mysqlError), "%s", vm_mysql_last_error());
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    printf("[error][mock-admin] monster_reset_failed id=%u error=%s\n",
           enemyId, mysqlError);
    if (errorOut)
        *errorOut = "??????????????? MySQL ??";
    return false;
}

static u32 vm_net_mock_monster_custom_count(void)
{
    u32 count = 0;

    if (!g_vm_net_mock_monster_catalog_ready)
        vm_net_mock_monster_catalog_reset_builtin();
    for (u32 i = 0; i < g_vm_net_mock_monster_catalog_count; ++i)
    {
        if (g_vm_net_mock_monster_custom[i])
            ++count;
    }
    return count;
}

static bool vm_net_mock_monster_admin_create(
    const vm_net_mock_monster_admin_row *row, const char **errorOut)
{
    char query[1280];
    char mysqlError[512];
    char nameHex[96];
    char sourceHex[160];
    char actorHex[160];
    const char *sourceLabel = "?????";
    bool transactionStarted = false;

    if (errorOut)
        *errorOut = "??????";
    if (row == NULL || row->enemyId == 0 || row->enemyId > 0xffffu ||
        row->level == 0 || row->level > 0xffu ||
        row->family > VM_NET_MOCK_MONSTER_BOSS || row->hp == 0 ||
        row->mp == 0 || row->attack == 0 ||
        row->hp > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->mp > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->attack > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->defense > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->exp > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->gold > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->dropCount > VM_NET_MOCK_MONSTER_DROP_MAX ||
        row->displayName[0] == 0 || row->actorResource[0] == 0 ||
        !vm_net_mock_str_ends_with(row->actorResource, ".actor") ||
        strlen(row->actorResource) >= sizeof(row->actorResource))
    {
        return false;
    }
    g_vm_net_mock_monster_db_loaded = false;
    if (!vm_net_mock_monster_db_load())
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (vm_net_mock_monster_catalog_index(row->enemyId) >= 0)
    {
        if (errorOut)
            *errorOut = "?? ID ???";
        return false;
    }
    if (vm_net_mock_monster_custom_count() >= VM_NET_MOCK_MONSTER_CUSTOM_MAX ||
        g_vm_net_mock_monster_catalog_count >= VM_NET_MOCK_MONSTER_CATALOG_MAX)
    {
        if (errorOut)
            *errorOut = "???????????";
        return false;
    }
    for (u8 i = 0; i < row->dropCount; ++i)
    {
        if (row->drops[i].itemId == 0 || row->drops[i].ratePercent == 0 ||
            row->drops[i].ratePercent > 100u ||
            vm_net_mock_find_shop_catalog_item(row->drops[i].itemId) == NULL)
        {
            if (errorOut)
                *errorOut = "???? ID ?????";
            return false;
        }
    }
    if (row->firstScene[0] != 0)
        sourceLabel = row->firstScene;
    memset(actorHex, 0, sizeof(actorHex));
    if (vm_mysql_hex_encode(row->displayName, strlen(row->displayName), nameHex,
                            sizeof(nameHex)) == 0 ||
        vm_mysql_hex_encode(sourceLabel, strlen(sourceLabel), sourceHex,
                            sizeof(sourceHex)) == 0 ||
        vm_mysql_hex_encode(row->actorResource, strlen(row->actorResource),
                            actorHex, sizeof(actorHex)) == 0)
    {
        if (errorOut)
            *errorOut = "????? Actor ????";
        return false;
    }
    if (!vm_mysql_exec("START TRANSACTION"))
        goto mysql_failed;
    transactionStarted = true;
    snprintf(query, sizeof(query),
             "INSERT INTO server_monster_catalog_extra("
             "monster_id,level,family,display_name,source_label,actor_resource) "
             "VALUES(%u,%u,%u,UNHEX('%s'),UNHEX('%s'),UNHEX('%s'))",
             row->enemyId, row->level, row->family, nameHex, sourceHex,
             actorHex);
    if (!vm_mysql_exec(query))
        goto mysql_failed;
    snprintf(
        query, sizeof(query),
        "INSERT INTO server_monsters(monster_id,level,family,cast_skill,actor_resource,"
        "hp,mp,attack_value,defense_value,reward_exp,reward_money,drop_item_id,"
        "drop_rate_percent) "
        "VALUES(%u,%u,%u,%u,UNHEX('%s'),%u,%u,%u,%u,%u,%u,0,0)",
        row->enemyId, row->level, row->family, row->castSkill ? 1u : 0u, actorHex,
        row->hp, row->mp, row->attack, row->defense, row->exp, row->gold);
    if (!vm_mysql_exec(query))
        goto mysql_failed;
    for (u8 i = 0; i < row->dropCount; ++i)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO server_monster_drops("
                 "monster_id,drop_slot,item_id,drop_rate_percent) "
                 "VALUES(%u,%u,%u,%u)",
                 row->enemyId, (u32)i + 1u, row->drops[i].itemId,
                 row->drops[i].ratePercent);
        if (!vm_mysql_exec(query))
            goto mysql_failed;
    }
    if (!vm_mysql_exec("COMMIT"))
        goto mysql_failed;
    transactionStarted = false;
    g_vm_net_mock_monster_db_loaded = false;
    if (!vm_net_mock_monster_db_load())
    {
        if (errorOut)
            *errorOut = "??????????????????";
        return false;
    }
    vm_net_mock_monster_resource_labels_invalidate();
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] monster_create id=%u level=%u family=%u cast_skill=%u "
           "actor=%s hp=%u name=%s catalog=%u\n",
           row->enemyId, row->level, row->family, row->castSkill ? 1u : 0u,
           row->actorResource, row->hp, row->displayName,
           g_vm_net_mock_monster_catalog_count);
    return true;

mysql_failed:
    snprintf(mysqlError, sizeof(mysqlError), "%s", vm_mysql_last_error());
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    printf("[error][mock-admin] monster_create_failed id=%u error=%s\n",
           row->enemyId, mysqlError);
    if (errorOut)
        *errorOut = "????????????? MySQL ??";
    return false;
}

static bool vm_net_mock_monster_admin_delete_custom(u32 enemyId,
                                                    const char **errorOut)
{
    char query[256];
    char mysqlError[512];
    bool transactionStarted = false;

    if (errorOut)
        *errorOut = "?????????";
    g_vm_net_mock_monster_db_loaded = false;
    if (!vm_net_mock_monster_db_load())
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (!vm_net_mock_monster_catalog_is_custom(enemyId))
        return false;
    if (!vm_mysql_exec("START TRANSACTION"))
        goto mysql_failed;
    transactionStarted = true;
    snprintf(query, sizeof(query),
             "DELETE FROM server_monster_drops WHERE monster_id=%u", enemyId);
    if (!vm_mysql_exec(query))
        goto mysql_failed;
    snprintf(query, sizeof(query),
             "DELETE FROM server_monsters WHERE monster_id=%u", enemyId);
    if (!vm_mysql_exec(query))
        goto mysql_failed;
    snprintf(query, sizeof(query),
             "DELETE FROM server_monster_catalog_extra WHERE monster_id=%u",
             enemyId);
    if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        goto mysql_failed;
    transactionStarted = false;
    g_vm_net_mock_monster_db_loaded = false;
    if (!vm_net_mock_monster_db_load())
    {
        if (errorOut)
            *errorOut = "???????????????";
        return false;
    }
    vm_net_mock_monster_resource_labels_invalidate();
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] monster_delete_custom id=%u catalog=%u\n",
           enemyId, g_vm_net_mock_monster_catalog_count);
    return true;

mysql_failed:
    snprintf(mysqlError, sizeof(mysqlError), "%s", vm_mysql_last_error());
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    printf("[error][mock-admin] monster_delete_custom_failed id=%u error=%s\n",
           enemyId, mysqlError);
    if (errorOut)
        *errorOut = "???????????????? MySQL ??";
    return false;
}

static u32 vm_net_mock_battle_enemy_ailment_adjust_defense(u32 baseDefense)
{
    u8 idx = g_vm_net_mock_battle_formula_enemy_index;

    if (idx >= 3 || g_mockBattleEnemyAilments[idx].stat.remainingRounds == 0)
        return baseDefense;
    return vm_net_mock_battle_apply_signed_stat_change(
        baseDefense, g_mockBattleEnemyAilments[idx].stat.defense, 65535u);
}

static u32 vm_net_mock_battle_enemy_ailment_adjust_attack(u32 baseAttack)
{
    u8 idx = g_vm_net_mock_battle_formula_enemy_index;

    if (idx >= 3 || g_mockBattleEnemyAilments[idx].stat.remainingRounds == 0)
        return baseAttack;
    return vm_net_mock_battle_apply_signed_stat_change(
        baseAttack, g_mockBattleEnemyAilments[idx].stat.attack +
                        g_mockBattleEnemyAilments[idx].stat.strength / 2, 65535u);
}

static u32 vm_net_mock_battle_enemy_ailment_adjust_hit(u32 baseHit)
{
    u8 idx = g_vm_net_mock_battle_formula_enemy_index;

    if (idx >= 3 || g_mockBattleEnemyAilments[idx].stat.remainingRounds == 0)
        return baseHit;
    return vm_net_mock_battle_apply_signed_stat_change(
        baseHit, g_mockBattleEnemyAilments[idx].stat.hit +
                     g_mockBattleEnemyAilments[idx].stat.agility * 2, 65535u);
}

static u32 vm_net_mock_battle_enemy_ailment_adjust_resist(u32 baseResist)
{
    u8 idx = g_vm_net_mock_battle_formula_enemy_index;

    if (idx >= 3 || g_mockBattleEnemyAilments[idx].stat.remainingRounds == 0)
        return baseResist;
    return vm_net_mock_battle_apply_signed_stat_change(
        baseResist, g_mockBattleEnemyAilments[idx].stat.resist +
                        g_mockBattleEnemyAilments[idx].stat.wisdom / 2, 65535u);
}

static void vm_net_mock_battle_role_stats_current(vm_net_mock_player_stats *statsOut)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (statsOut == NULL)
        return;
    memset(statsOut, 0, sizeof(*statsOut));
    vm_net_mock_role_build_player_stats(role, statsOut);
    vm_net_mock_battle_apply_active_stat_modifier(statsOut);
}

static u32 vm_net_mock_battle_role_attack_default(void)
{
    vm_net_mock_player_stats stats;
    vm_net_mock_battle_role_stats_current(&stats);
    return stats.attack ? stats.attack : 1;
}

static u32 vm_net_mock_battle_role_defense_default(void)
{
    vm_net_mock_player_stats stats;
    vm_net_mock_battle_role_stats_current(&stats);
    return vm_net_mock_battle_mitigation_defense(&stats);
}

/*
 * Hit / dodge / crit / resist combat rolls.
 *
 * Original client formula is still unresolved (no recovered battle-math
 * table).  Derived player stats already look %-like (hit~75+, dodge~3+,
 * crit~1+), so provisional contract:
 *   hitChance = clamp(attacker.hit - defender.dodge, 5, 95)
 *   critChance = clamp(attacker.crit, 0, 100); crit multiplies by 150%
 *   physical mitigation = damage_after_defense(raw, defense)  (K=2000)
 *   spell mitigation   = damage_after_resist(raw, resist)
 *     resist is a percent reduction, clamped to 70%
 *     (wisdomCoeff dominant among ?/?/???)
 *   Player hit = 101 + equipment.hit; dodge/crit/resist = equipment columns only
 *   Player NA defense = bare + equip.armor/5 (panel still full armor)
 *   Monster resist defaults to 0 (CBE_BATTLE_MONSTER_RESIST)
 *   Monster NA defense = catalog/MySQL stats.defense (CBE_BATTLE_ENEMY_DEFENSE)
 *   Offensive skills (physical + spell) mitigate with monster resist %
 *     (CBE_BATTLE_SKILL_ENEMY_RESIST / CBE_BATTLE_MONSTER_RESIST)
 * Miss/dodge returns damage 0; actioninfo keeps a child with valueA=0 and
 * child_flag=3 (dodge). Crit sets child_flag=2 (crit).
 * Hit that would mitigate to 0 still floors to 1; do not coerce miss.
 * Damage variance (before crit; PvE + duel share admin percents):
 *   NA / monster NA counter ±na_variance_percent (default 5)
 *   offensive skill / monster skill ±skill_variance_percent (default 8)
 * Env: CBE_BATTLE_DISABLE_COMBAT_ROLLS=1 restores always-hit pre-roll path
 *      and disables variance; CBE_BATTLE_FORCE_HIT / FORCE_MISS / FORCE_CRIT
 *      override hit/crit rolls.
 */
static u32 vm_net_mock_battle_combat_rand(void)
{
    /* Reuse the battle reward xorshift state (same TU). */
    if (g_vm_net_mock_battle_reward_rng == 0)
    {
        g_vm_net_mock_battle_reward_rng =
            0xC0FFEE01u ^
            (g_schedulerTick * 1664525u) ^
            (g_mockBattleOperateSessionSerial * 1013904223u) ^
            (g_vm_net_mock_role_db.activeRoleId << 1);
        if (g_vm_net_mock_battle_reward_rng == 0)
            g_vm_net_mock_battle_reward_rng = 0x9e3779b9u;
    }
    g_vm_net_mock_battle_reward_rng ^= g_vm_net_mock_battle_reward_rng << 13;
    g_vm_net_mock_battle_reward_rng ^= g_vm_net_mock_battle_reward_rng >> 17;
    g_vm_net_mock_battle_reward_rng ^= g_vm_net_mock_battle_reward_rng << 5;
    return g_vm_net_mock_battle_reward_rng;
}

static u32 vm_net_mock_battle_combat_roll_percent(void)
{
    return vm_net_mock_battle_combat_rand() % 100u;
}

static bool vm_net_mock_battle_combat_rolls_disabled(void)
{
    return vm_net_mock_env_u32("CBE_BATTLE_DISABLE_COMBAT_ROLLS", 0) != 0;
}

static void vm_net_mock_monster_secondary_combat_stats(u32 level,
                                                      u32 *hitOut,
                                                      u32 *dodgeOut,
                                                      u32 *critOut,
                                                      u32 *resistOut)
{
    const vm_net_mock_combat_formula_config *combatCfg =
        vm_net_mock_combat_formula_config_get();
    u32 defHit = combatCfg ? combatCfg->monsterHitDefault : 100u;
    u32 defDodge = combatCfg ? combatCfg->monsterDodgeDefault : 0u;
    u32 defCrit = combatCfg ? combatCfg->monsterCritDefault : 0u;
    u32 defResist = combatCfg ? combatCfg->monsterResistDefault : 0u;

    (void)level;

    /* Hit/dodge/crit/resist stay fixed (no automonster columns yet).
     * Admin defaults apply; CBE_BATTLE_MONSTER_* env still overrides. */
    if (hitOut)
        *hitOut = vm_net_mock_env_u32("CBE_BATTLE_MONSTER_HIT", defHit);
    if (dodgeOut)
        *dodgeOut = vm_net_mock_env_u32("CBE_BATTLE_MONSTER_DODGE", defDodge);
    if (critOut)
        *critOut = vm_net_mock_env_u32("CBE_BATTLE_MONSTER_CRIT", defCrit);
    if (resistOut)
        *resistOut = vm_net_mock_env_u32("CBE_BATTLE_MONSTER_RESIST", defResist);
}

static u32 vm_net_mock_battle_hit_chance(u32 hit, u32 dodge)
{
    const vm_net_mock_combat_formula_config *combatCfg =
        vm_net_mock_combat_formula_config_get();
    u32 chanceMin = combatCfg ? combatCfg->hitChanceMin : 5u;
    u32 chanceMax = combatCfg ? combatCfg->hitChanceMax : 95u;
    u32 chance;

    if (hit > dodge)
        chance = hit - dodge;
    else
        chance = 0;
    if (chance < chanceMin)
        chance = chanceMin;
    if (chance > chanceMax)
        chance = chanceMax;
    return chance;
}

/*
 * mmBattleMstarWqvga.cbm VA 0x24f6 (callers 0x504c/0x511c/0x53fe):
 *   ldrb r0, [slot+2]  ; action child_flag
 *   bl   0x24f6        ; if flag==2 copy ????, if flag==3 copy ????
 * Evidence: ADR at 0x2506?????, 0x250c?????; cmp r2,#2 / cmp r2,#3.
 */
enum
{
    VM_NET_MOCK_BATTLE_CHILD_FLAG_NORMAL = 0,
    VM_NET_MOCK_BATTLE_CHILD_FLAG_CRIT = 2,
    VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE = 3
};

/* Flat attack bonus applied to all player job offensive skill raw damage. */
static u8 g_mockBattleLastOutcomeChildFlag = 0;

static void vm_net_mock_battle_clear_outcome_child_flag(void)
{
    g_mockBattleLastOutcomeChildFlag = VM_NET_MOCK_BATTLE_CHILD_FLAG_NORMAL;
}

static void vm_net_mock_battle_note_outcome_child_flag(u8 flag)
{
    g_mockBattleLastOutcomeChildFlag = flag;
}

static u8 vm_net_mock_battle_take_outcome_child_flag(void)
{
    u8 flag = g_mockBattleLastOutcomeChildFlag;
    g_mockBattleLastOutcomeChildFlag = VM_NET_MOCK_BATTLE_CHILD_FLAG_NORMAL;
    return flag;
}

static u8 vm_net_mock_battle_child_flag_with_env(const char *envName, u8 computed)
{
    const char *spec = getenv(envName);

    if (spec != NULL && spec[0] != 0)
        return (u8)vm_net_mock_env_u32(envName, computed);
    return computed;
}

static bool vm_net_mock_battle_roll_hit(u32 attackerHit, u32 defenderDodge)
{
    u32 forceHit;
    u32 forceMiss;
    u32 chance;
    u32 roll;

    if (vm_net_mock_battle_combat_rolls_disabled())
        return true;
    forceHit = vm_net_mock_env_u32("CBE_BATTLE_FORCE_HIT", 0);
    forceMiss = vm_net_mock_env_u32("CBE_BATTLE_FORCE_MISS", 0);
    if (forceMiss != 0)
        return false;
    if (forceHit != 0)
        return true;
    chance = vm_net_mock_battle_hit_chance(attackerHit, defenderDodge);
    roll = vm_net_mock_battle_combat_roll_percent();
    return roll < chance;
}

static bool vm_net_mock_battle_roll_crit(u32 crit)
{
    u32 forceCrit;
    u32 rate;
    u32 roll;

    if (vm_net_mock_battle_combat_rolls_disabled())
        return false;
    forceCrit = vm_net_mock_env_u32("CBE_BATTLE_FORCE_CRIT", 2);
    /* 0=never, 1=always, 2=unset ? roll */
    if (forceCrit == 0)
        return false;
    if (forceCrit == 1)
        return true;
    rate = crit > 100u ? 100u : crit;
    if (rate == 0)
        return false;
    roll = vm_net_mock_battle_combat_roll_percent();
    return roll < rate;
}

static u32 vm_net_mock_battle_apply_crit_damage(u32 damage)
{
    const vm_net_mock_combat_formula_config *combatCfg =
        vm_net_mock_combat_formula_config_get();
    u32 critPct = combatCfg ? combatCfg->critDamagePercent : 150u;
    uint64_t boosted;

    if (damage == 0)
        return 0;
    if (critPct < 100u)
        critPct = 100u;
    /* Provisional crit mult; original CBE multiplier unresolved. */
    boosted = ((uint64_t)damage * (uint64_t)critPct + 50ull) / 100ull;
    if (boosted < (uint64_t)damage + 1ull)
        boosted = (uint64_t)damage + 1ull;
    return boosted > 0xffffffffull ? 0xffffffffu : (u32)boosted;
}

static u32 vm_net_mock_battle_apply_damage_variance(u32 damage)
{
    const vm_net_mock_combat_formula_config *combatCfg =
        vm_net_mock_combat_formula_config_get();
    u32 variancePct = combatCfg ? combatCfg->naVariancePercent : 5u;
    u32 span;
    u32 roll;
    u32 pct;
    uint64_t scaled;

    if (damage == 0)
        return 0;
    if (vm_net_mock_battle_combat_rolls_disabled())
        return damage;
    if (variancePct > 50u)
        variancePct = 50u;
    span = variancePct * 2u + 1u;
    roll = vm_net_mock_battle_combat_rand() % span;
    pct = (100u - variancePct) + roll;
    scaled = ((uint64_t)damage * (uint64_t)pct + 50ull) / 100ull;
    if (scaled == 0)
        scaled = 1;
    return scaled > 0xffffffffull ? 0xffffffffu : (u32)scaled;
}

static u32 vm_net_mock_battle_apply_skill_damage_variance(u32 damage)
{
    const vm_net_mock_combat_formula_config *combatCfg =
        vm_net_mock_combat_formula_config_get();
    u32 variancePct = combatCfg ? combatCfg->skillVariancePercent : 8u;
    u32 span;
    u32 roll;
    u32 pct;
    uint64_t scaled;

    if (damage == 0)
        return 0;
    if (vm_net_mock_battle_combat_rolls_disabled())
        return damage;
    if (variancePct > 50u)
        variancePct = 50u;
    span = variancePct * 2u + 1u;
    roll = vm_net_mock_battle_combat_rand() % span;
    pct = (100u - variancePct) + roll;
    scaled = ((uint64_t)damage * (uint64_t)pct + 50ull) / 100ull;
    if (scaled == 0)
        scaled = 1;
    return scaled > 0xffffffffull ? 0xffffffffu : (u32)scaled;
}

static u32 vm_net_mock_damage_after_resist(u32 damage, u32 resist)
{
    const vm_net_mock_combat_formula_config *combatCfg =
        vm_net_mock_combat_formula_config_get();
    u32 resistCap = combatCfg ? combatCfg->resistCapPercent : 70u;
    u32 pct;
    uint64_t kept;

    /* Resist is a flat percent reduction on spell damage; admin hard-cap. */
    if (damage == 0)
        return 0;
    if (resistCap > 100u)
        resistCap = 100u;
    pct = resist > resistCap ? resistCap : resist;
    if (pct == 0)
        return damage;
    kept = ((uint64_t)damage * (100ull - (uint64_t)pct)) / 100ull;
    return kept > 0xffffffffull ? 0xffffffffu : (u32)kept;
}

/*
 * Pixi特效称号 incoming DR: +1% per unlocked chenghao id (CSV), cap 45%.
 * PVP halves the percent (integer). Applied after armor/resist, before HP clamp.
 */
enum
{
    VM_NET_MOCK_CHENGHAO_DR_PERCENT_CAP = 45
};

static u32 vm_net_mock_cosmetic_csv_count_ids(const char *csv)
{
    u32 n = 0;
    const char *p = csv;

    if (p == NULL || p[0] == 0)
        return 0;
    while (*p)
    {
        while (*p == ',' || *p == ' ')
            ++p;
        if (*p == 0)
            break;
        ++n;
        while (*p && *p != ',')
            ++p;
    }
    return n;
}

static u32 vm_net_mock_role_chenghao_incoming_dr_percent(
    const vm_net_mock_role_state *role,
    bool pvp)
{
    u32 n;

    if (role == NULL)
        return 0;
    n = vm_net_mock_cosmetic_csv_count_ids(role->unlockedChenghaoIds);
    if (n > (u32)VM_NET_MOCK_CHENGHAO_DR_PERCENT_CAP)
        n = (u32)VM_NET_MOCK_CHENGHAO_DR_PERCENT_CAP;
    if (pvp)
        n /= 2u;
    return n;
}

static u32 vm_net_mock_battle_apply_chenghao_incoming_dr(
    u32 damage,
    const vm_net_mock_role_state *role,
    bool pvp)
{
    u32 pct;
    uint64_t kept;

    if (damage == 0 || role == NULL)
        return damage;
    pct = vm_net_mock_role_chenghao_incoming_dr_percent(role, pvp);
    if (pct == 0)
        return damage;
    kept = ((uint64_t)damage * (100ull - (uint64_t)pct)) / 100ull;
    return kept > 0xffffffffull ? 0xffffffffu : (u32)kept;
}

static bool vm_net_mock_battle_skill_is_magical(
    const vm_net_mock_skill_catalog_item *skill)
{
    if (skill == NULL)
        return false;
    if (skill->wisdomCoeff == 0)
        return false;
    return skill->wisdomCoeff >= skill->strengthCoeff &&
           skill->wisdomCoeff >= skill->agilityCoeff;
}

/* ?? (job 3) ????????????????????? */
static bool vm_net_mock_battle_skill_ignores_dodge(
    const vm_net_mock_skill_catalog_item *skill)
{
    return vm_net_mock_battle_skill_is_magical(skill);
}

static u32 vm_net_mock_role_active_combat_pill_attack_bonus_percent(
    const vm_net_mock_role_state *role);

static u32 vm_net_mock_battle_player_damage_to_enemy(u32 enemyId, u32 enemyHpCurrent)
{
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(enemyId);
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_player_stats playerStats;
    u32 attack = 0;
    /* MySQL/admin defense_value (and formula fallback) owns NA mitigation. */
    u32 defense = vm_net_mock_env_u32_if_set("CBE_BATTLE_ENEMY_DEFENSE",
                                            stats.defense);
    u32 monsterDodge = 0;
    u32 pillBonusPercent = 0;
    u32 damage = 0;
    bool critical = false;

    vm_net_mock_battle_clear_outcome_child_flag();
    if (enemyHpCurrent == 0)
        return 0;

    vm_net_mock_battle_role_stats_current(&playerStats);
    vm_net_mock_monster_secondary_combat_stats(stats.level, NULL, &monsterDodge, NULL,
                                              NULL);
    if (!vm_net_mock_battle_roll_hit(playerStats.hit, monsterDodge))
    {
        vm_net_mock_battle_note_outcome_child_flag(VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE);
        printf("[info][network] mock_battle_hit_roll side=player_atk result=miss "
               "hit=%u dodge=%u enemy=%u child_flag=3 evidence=cbm-0x24f6-dodge\n",
               playerStats.hit, monsterDodge, enemyId);
        return 0;
    }

    attack = vm_net_mock_env_u32_if_set("CBE_BATTLE_PLAYER_ATTACK",
                                       playerStats.attack ? playerStats.attack : 1);
    pillBonusPercent = vm_net_mock_role_active_combat_pill_attack_bonus_percent(role);
    if (pillBonusPercent != 0)
    {
        uint64_t boosted = (uint64_t)attack * (100ull + pillBonusPercent) / 100ull;
        attack = boosted > 0xffffffffull ? 0xffffffffu : (u32)boosted;
    }
    defense = vm_net_mock_battle_enemy_ailment_adjust_defense(defense);
    damage = vm_net_mock_damage_after_defense(attack, defense);
    if (damage == 0)
        damage = 1;
    damage = vm_net_mock_battle_apply_damage_variance(damage);
    critical = vm_net_mock_battle_roll_crit(playerStats.crit);
    if (critical)
        damage = vm_net_mock_battle_apply_crit_damage(damage);
    if (critical)
    {
        vm_net_mock_battle_note_outcome_child_flag(VM_NET_MOCK_BATTLE_CHILD_FLAG_CRIT);
        printf("[info][network] mock_battle_hit_roll side=player_atk result=crit "
               "crit=%u damage=%u enemy=%u child_flag=2 evidence=cbm-0x24f6-crit\n",
               playerStats.crit, damage, enemyId);
    }
    return vm_net_mock_min_u32(damage, enemyHpCurrent);
}

static u32 vm_net_mock_battle_skill_min_hp_damage(const vm_net_mock_skill_catalog_item *skill)
{
    if (skill == NULL || skill->hpChange >= 0)
        return 0;
    return (u32)(0 - skill->hpChange);
}

static u32 vm_net_mock_battle_player_skill_damage_to_enemy(u32 operate, u32 enemyId,
                                                           u32 enemyHpCurrent)
{
    const vm_net_mock_skill_catalog_item *skill = vm_net_mock_battle_operate_skill(operate);
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_player_stats playerStats;
    vm_net_mock_monster_stats monsterStats = vm_net_mock_monster_stats_for_enemy(enemyId);
    u32 baseDamage = vm_net_mock_battle_skill_min_hp_damage(skill);
    uint64_t coeffDamage = 0;
    u32 rawDamage = 0;
    u32 mitigation = 0;
    u32 monsterDodge = 0;
    u32 monsterResist = 0;
    u32 damage = 0;
    bool magical = false;
    bool critical = false;

    vm_net_mock_battle_clear_outcome_child_flag();
    if (enemyHpCurrent == 0)
        return 0;
    if (skill == NULL || baseDamage == 0)
    {
        /* Support skills (heal/buff/status) must never fall back to ATK. */
        if (skill != NULL &&
            (skill->targetDirection <= 2 || skill->hpChange >= 0))
        {
            return 0;
        }
        return vm_net_mock_battle_player_damage_to_enemy(enemyId, enemyHpCurrent);
    }

    vm_net_mock_battle_role_stats_current(&playerStats);
    vm_net_mock_monster_secondary_combat_stats(monsterStats.level, NULL, &monsterDodge,
                                              NULL, &monsterResist);
    magical = vm_net_mock_battle_skill_is_magical(skill);
    /* Player evidence: ?????????????????????/????? */
    if (!vm_net_mock_battle_skill_ignores_dodge(skill) &&
        !vm_net_mock_battle_roll_hit(playerStats.hit, monsterDodge))
    {
        vm_net_mock_battle_note_outcome_child_flag(VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE);
        printf("[info][network] mock_battle_hit_roll side=player_skill result=miss "
               "operate=%u hit=%u dodge=%u enemy=%u child_flag=3 evidence=cbm-0x24f6-dodge\n",
               operate, playerStats.hit, monsterDodge, enemyId);
        return 0;
    }

    coeffDamage += (uint64_t)playerStats.strength * skill->strengthCoeff;
    coeffDamage += (uint64_t)playerStats.agility * skill->agilityCoeff;
    coeffDamage += (uint64_t)playerStats.wisdom * skill->wisdomCoeff;
    coeffDamage = (coeffDamage + 50u) / 100u;
    if (coeffDamage > 0xffffffffull - baseDamage)
        rawDamage = 0xffffffffu;
    else
        rawDamage = baseDamage + (u32)coeffDamage;

    /*
     * Play contract: every learned job offensive skill gets a flat admin
     * job_skill_attack_bonus_percent on raw damage (before resist/defense).
     * Stacks multiplicatively with combat-pill attack percent.
     */
    {
        const vm_net_mock_combat_formula_config *combatCfg =
            vm_net_mock_combat_formula_config_get();
        u32 skillBonusPct =
            combatCfg ? combatCfg->jobSkillAttackBonusPercent : 5u;
        uint64_t skillBoosted =
            (uint64_t)rawDamage * (100ull + skillBonusPct) / 100ull;
        rawDamage =
            skillBoosted > 0xffffffffull ? 0xffffffffu : (u32)skillBoosted;
    }

    {
        u32 pillBonusPercent =
            vm_net_mock_role_active_combat_pill_attack_bonus_percent(role);
        if (pillBonusPercent != 0)
        {
            uint64_t boosted =
                (uint64_t)rawDamage * (100ull + pillBonusPercent) / 100ull;
            rawDamage = boosted > 0xffffffffull ? 0xffffffffu : (u32)boosted;
        }
    }

    /*
     * Offensive skills (physical + magical) share resist % mitigation.
     * Catalog/admin defense applies to NA only; skill path uses secondary
     * monster resist (default 0 until automonster columns exist).
     */
    mitigation = vm_net_mock_env_u32_if_set("CBE_BATTLE_SKILL_ENEMY_RESIST",
                                           monsterResist);
    mitigation = vm_net_mock_battle_enemy_ailment_adjust_resist(mitigation);
    damage = vm_net_mock_damage_after_resist(rawDamage, mitigation);
    if (damage < baseDamage)
        damage = baseDamage;
    damage = vm_net_mock_env_u32_if_set("CBE_BATTLE_SKILL_DAMAGE", damage);
    if (damage == 0)
        damage = 1;
    damage = vm_net_mock_battle_apply_skill_damage_variance(damage);
    critical = vm_net_mock_battle_roll_crit(playerStats.crit);
    if (critical)
        damage = vm_net_mock_battle_apply_crit_damage(damage);
    if (critical)
        vm_net_mock_battle_note_outcome_child_flag(VM_NET_MOCK_BATTLE_CHILD_FLAG_CRIT);
    if (critical || magical)
    {
        printf("[info][network] mock_battle_hit_roll side=player_skill "
               "result=%s magical=%u resist_or_def=%u crit_stat=%u damage=%u "
               "operate=%u enemy=%u child_flag=%u evidence=cbm-0x24f6\n",
               critical ? "crit" : "hit", magical ? 1u : 0u, mitigation,
               playerStats.crit, damage, operate, enemyId,
               critical ? (u32)VM_NET_MOCK_BATTLE_CHILD_FLAG_CRIT : 0u);
    }
    return vm_net_mock_min_u32(damage, enemyHpCurrent);
}

/*
 * Monster → player damage.
 *   skillMitigateWithResist=0: normal attack — soft armor
 *     (bare + equip.armor/5, K=2000)
 *   skillMitigateWithResist=1: monster skill — player 法抗 % only (no 护甲)
 */
static u32 vm_net_mock_battle_enemy_damage_to_role_ex(u32 enemyId,
                                                     u32 roleHpCurrent,
                                                     bool skillMitigateWithResist)
{
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(enemyId);
    vm_net_mock_player_stats playerStats;
    u32 attack = vm_net_mock_env_u32_if_set("CBE_BATTLE_ENEMY_ATTACK", stats.attack);
    u32 defense = 0;
    u32 resist = 0;
    u32 monsterHit = 0;
    u32 monsterCrit = 0;
    u32 damage = 0;
    bool critical = false;

    vm_net_mock_battle_clear_outcome_child_flag();
    if (roleHpCurrent == 0)
        return 0;

    vm_net_mock_battle_role_stats_current(&playerStats);
    defense = vm_net_mock_env_u32_if_set(
        "CBE_BATTLE_ROLE_DEFENSE",
        vm_net_mock_battle_mitigation_defense(&playerStats));
    resist = vm_net_mock_env_u32_if_set("CBE_BATTLE_ROLE_RESIST", playerStats.resist);
    vm_net_mock_monster_secondary_combat_stats(stats.level, &monsterHit, NULL,
                                              &monsterCrit, NULL);
    attack = vm_net_mock_battle_enemy_ailment_adjust_attack(attack);
    monsterHit = vm_net_mock_battle_enemy_ailment_adjust_hit(monsterHit);
    if (!vm_net_mock_battle_roll_hit(monsterHit, playerStats.dodge))
    {
        vm_net_mock_battle_note_outcome_child_flag(VM_NET_MOCK_BATTLE_CHILD_FLAG_DODGE);
        printf("[info][network] mock_battle_hit_roll side=enemy_atk result=miss "
               "hit=%u dodge=%u enemy=%u child_flag=3 evidence=cbm-0x24f6-dodge\n",
               monsterHit, playerStats.dodge, enemyId);
        return 0;
    }

    if (skillMitigateWithResist)
    {
        damage = vm_net_mock_damage_after_resist(attack, resist);
        printf("[info][network] mock_battle_enemy_skill_mitigate enemy=%u "
               "attack=%u resist=%u damage=%u defense_ignored=%u "
               "evidence=monster-skill-resist-only\n",
               enemyId, attack, resist, damage, defense);
    }
    else
    {
        damage = vm_net_mock_damage_after_defense(attack, defense);
    }
    if (damage == 0)
        damage = 1;
    if (skillMitigateWithResist)
        damage = vm_net_mock_battle_apply_skill_damage_variance(damage);
    else
        damage = vm_net_mock_battle_apply_damage_variance(damage);
    critical = vm_net_mock_battle_roll_crit(monsterCrit);
    if (critical)
    {
        damage = vm_net_mock_battle_apply_crit_damage(damage);
        vm_net_mock_battle_note_outcome_child_flag(VM_NET_MOCK_BATTLE_CHILD_FLAG_CRIT);
    }
    {
        vm_net_mock_role_state *role = vm_net_mock_active_role();
        u32 beforeDr = damage;
        damage = vm_net_mock_battle_apply_chenghao_incoming_dr(damage, role, false);
        if (beforeDr != damage)
        {
            printf("[info][network] mock_battle_chenghao_dr side=pve enemy=%u "
                   "before=%u after=%u pct=%u unlocks=%u\n",
                   enemyId, beforeDr, damage,
                   vm_net_mock_role_chenghao_incoming_dr_percent(role, false),
                   role ? vm_net_mock_cosmetic_csv_count_ids(role->unlockedChenghaoIds)
                        : 0u);
        }
    }
    return vm_net_mock_min_u32(damage, roleHpCurrent);
}

static u32 vm_net_mock_battle_enemy_damage_to_role(u32 enemyId, u32 roleHpCurrent)
{
    return vm_net_mock_battle_enemy_damage_to_role_ex(enemyId, roleHpCurrent,
                                                     false);
}

/* Remitigate an already-connected hit with 法抗 only (no second hit roll). */
static u32 vm_net_mock_battle_enemy_skill_remitigate_to_role(u32 enemyId,
                                                            u32 roleHpCurrent,
                                                            bool keepCrit)
{
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(enemyId);
    vm_net_mock_player_stats playerStats;
    u32 attack = vm_net_mock_env_u32_if_set("CBE_BATTLE_ENEMY_ATTACK", stats.attack);
    u32 resist = 0;
    u32 damage = 0;

    if (roleHpCurrent == 0)
        return 0;
    vm_net_mock_battle_role_stats_current(&playerStats);
    resist = vm_net_mock_env_u32_if_set("CBE_BATTLE_ROLE_RESIST", playerStats.resist);
    attack = vm_net_mock_battle_enemy_ailment_adjust_attack(attack);
    damage = vm_net_mock_damage_after_resist(attack, resist);
    if (damage == 0)
        damage = 1;
    damage = vm_net_mock_battle_apply_skill_damage_variance(damage);
    if (keepCrit)
        damage = vm_net_mock_battle_apply_crit_damage(damage);
    {
        vm_net_mock_role_state *role = vm_net_mock_active_role();
        u32 beforeDr = damage;
        damage = vm_net_mock_battle_apply_chenghao_incoming_dr(damage, role, false);
        if (beforeDr != damage)
        {
            printf("[info][network] mock_battle_chenghao_dr side=pve-skill enemy=%u "
                   "before=%u after=%u pct=%u\n",
                   enemyId, beforeDr, damage,
                   vm_net_mock_role_chenghao_incoming_dr_percent(role, false));
        }
    }
    printf("[info][network] mock_battle_enemy_skill_mitigate enemy=%u "
           "attack=%u resist=%u damage=%u crit=%u "
           "evidence=monster-skill-resist-only\n",
           enemyId, attack, resist, damage, keepCrit ? 1u : 0u);
    return vm_net_mock_min_u32(damage, roleHpCurrent);
}

static const char *vm_net_mock_role_initial_scene_name(void)
{
    return vm_net_mock_default_scene_name();
}

static u32 vm_net_mock_role_default_weapon_for_job(u32 job)
{
    switch (job)
    {
    case 2:
        return 1501; /* starter dagger */
    case 3:
        return 2001; /* starter staff */
    case 1:
    default:
        return 1001; /* starter sword */
    }
}

/*
 * Weapon equip.dsh categories are job-locked:
 *   7 天机 / 8 幻剑 / 9 鬼道.
 * Non-weapon categories (armor/ring/etc.) are unrestricted.
 */
static bool vm_net_mock_role_job_allows_weapon_category(u8 job, u8 category)
{
    if (category < 7u || category > 9u)
        return true;
    switch (job)
    {
    case 1:
        return category == 7u;
    case 2:
        return category == 8u;
    case 3:
        return category == 9u;
    default:
        return false;
    }
}

static void vm_net_mock_role_init_default_equipment(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    memset(role->equippedItemIds, 0, sizeof(role->equippedItemIds));
    memset(role->equippedEnhanceLevels, 0, sizeof(role->equippedEnhanceLevels));
    memset(role->equippedEnhanceWash, 0, sizeof(role->equippedEnhanceWash));
    role->equippedItemIds[0] = vm_net_mock_role_default_weapon_for_job(role->job);
}

static void vm_net_mock_role_init_default_backpack(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    memset(role->backpackItems, 0, sizeof(role->backpackItems));
    role->backpackCapacity = VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY;
    role->backpackItemCount = 0;
    role->nextBackpackSeq = 1;
}

static bool vm_net_mock_role_has_default_name(const vm_net_mock_role_state *role)
{
    return role != NULL && strcmp(role->name, vm_net_mock_default_role_name()) == 0;
}

static void vm_net_mock_role_assign_fallback_name(vm_net_mock_role_state *role)
{
    u32 roleId = VM_NET_MOCK_ROLE_DEFAULT_ID;
    if (role == NULL)
        return;
    if (role->roleId != 0)
        roleId = role->roleId;
    snprintf(role->name, sizeof(role->name), "Role%u", roleId);
}

static void vm_net_mock_role_init_default(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    memset(role, 0, sizeof(*role));
    role->roleId = VM_NET_MOCK_ROLE_DEFAULT_ID;
    snprintf(role->name, sizeof(role->name), "%s", vm_net_mock_default_role_name());
    role->job = 1;
    role->sex = 0;
    role->backpackCapacity = VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY;
    role->level = 1;
    role->exp = 0;
    role->hp = VM_NET_MOCK_ROLE_DEFAULT_HP;
    role->hpMax = VM_NET_MOCK_ROLE_DEFAULT_HP;
    role->mp = VM_NET_MOCK_ROLE_DEFAULT_MP;
    role->mpMax = VM_NET_MOCK_ROLE_DEFAULT_MP;
    role->money = VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    role->wcoin = 0;
    snprintf(role->scene, sizeof(role->scene), "%s", vm_net_mock_role_initial_scene_name());
    role->x = VM_NET_MOCK_ROLE_INITIAL_X;
    role->y = VM_NET_MOCK_ROLE_INITIAL_Y;
    if (!vm_net_mock_scene_is_penglai01(role->scene))
    {
        (void)vm_net_mock_get_scene_reasonable_spawn_from_sce(role->scene,
                                                              &role->x,
                                                              &role->y,
                                                              NULL);
    }
    role->designationId = 0;
    role->arenaDesignationMask = 0;
    role->specialDesignationMask = 0;
    role->createdAtUnix = 0;
    vm_net_mock_role_init_default_equipment(role);
    vm_net_mock_role_init_default_backpack(role);
    vm_net_mock_role_sync_derived_vitals(role);
}

static void vm_net_mock_role_copy_from_v1(vm_net_mock_role_state *dst,
                                          const vm_net_mock_role_state_v1 *src)
{
    if (dst == NULL || src == NULL)
        return;
    memset(dst, 0, sizeof(*dst));
    dst->roleId = src->roleId;
    memcpy(dst->name, src->name, sizeof(dst->name));
    dst->job = src->job;
    dst->sex = src->sex;
    dst->backpackCapacity = src->backpackCapacity;
    dst->level = src->level;
    dst->exp = src->exp;
    dst->hp = src->hp;
    dst->hpMax = src->hpMax;
    dst->mp = src->mp;
    dst->mpMax = src->mpMax;
    dst->money = src->money;
    memcpy(dst->scene, src->scene, sizeof(dst->scene));
    dst->x = src->x;
    dst->y = src->y;
    vm_net_mock_role_init_default_equipment(dst);
    vm_net_mock_role_init_default_backpack(dst);
}

static void vm_net_mock_role_copy_from_v2(vm_net_mock_role_state *dst,
                                          const vm_net_mock_role_state_v2 *src)
{
    if (dst == NULL || src == NULL)
        return;
    memset(dst, 0, sizeof(*dst));
    dst->roleId = src->roleId;
    memcpy(dst->name, src->name, sizeof(dst->name));
    dst->job = src->job;
    dst->sex = src->sex;
    dst->backpackCapacity = src->backpackCapacity;
    dst->level = src->level;
    dst->exp = src->exp;
    dst->hp = src->hp;
    dst->hpMax = src->hpMax;
    dst->mp = src->mp;
    dst->mpMax = src->mpMax;
    dst->money = src->money;
    memcpy(dst->scene, src->scene, sizeof(dst->scene));
    dst->x = src->x;
    dst->y = src->y;
    dst->backpackItemCount = src->backpackItemCount;
    dst->designationId = 0;
    dst->nextBackpackSeq = src->nextBackpackSeq;
    memcpy(dst->backpackItems, src->backpackItems, sizeof(src->backpackItems));
    vm_net_mock_role_init_default_equipment(dst);
    vm_net_mock_role_migrate_legacy_backpack_capacity(dst);
}

static void vm_net_mock_role_copy_from_v3(vm_net_mock_role_state *dst,
                                          const vm_net_mock_role_state_v3 *src)
{
    if (dst == NULL || src == NULL)
        return;
    memset(dst, 0, sizeof(*dst));
    dst->roleId = src->roleId;
    memcpy(dst->name, src->name, sizeof(dst->name));
    dst->job = src->job;
    dst->sex = src->sex;
    dst->backpackCapacity = src->backpackCapacity;
    dst->level = src->level;
    dst->exp = src->exp;
    dst->hp = src->hp;
    dst->hpMax = src->hpMax;
    dst->mp = src->mp;
    dst->mpMax = src->mpMax;
    dst->money = src->money;
    memcpy(dst->scene, src->scene, sizeof(dst->scene));
    dst->x = src->x;
    dst->y = src->y;
    dst->backpackItemCount = src->backpackItemCount;
    dst->designationId = src->designationId;
    dst->nextBackpackSeq = src->nextBackpackSeq;
    memcpy(dst->equippedItemIds, src->equippedItemIds, sizeof(src->equippedItemIds));
    memcpy(dst->backpackItems, src->backpackItems, sizeof(src->backpackItems));
    vm_net_mock_role_migrate_legacy_backpack_capacity(dst);
}

static void vm_net_mock_role_copy_from_v4(vm_net_mock_role_state *dst,
                                          const vm_net_mock_role_state_v4 *src)
{
    if (dst == NULL || src == NULL)
        return;
    memset(dst, 0, sizeof(*dst));
    dst->roleId = src->roleId;
    memcpy(dst->name, src->name, sizeof(dst->name));
    dst->job = src->job;
    dst->sex = src->sex;
    dst->backpackCapacity = src->backpackCapacity;
    dst->level = src->level;
    dst->exp = src->exp;
    dst->hp = src->hp;
    dst->hpMax = src->hpMax;
    dst->mp = src->mp;
    dst->mpMax = src->mpMax;
    dst->money = src->money;
    dst->wcoin = 0;
    memcpy(dst->scene, src->scene, sizeof(dst->scene));
    dst->x = src->x;
    dst->y = src->y;
    dst->backpackItemCount = src->backpackItemCount;
    dst->designationId = src->designationId;
    dst->nextBackpackSeq = src->nextBackpackSeq;
    memcpy(dst->equippedItemIds, src->equippedItemIds, sizeof(src->equippedItemIds));
    memcpy(dst->backpackItems, src->backpackItems, sizeof(src->backpackItems));
}

static void vm_net_mock_role_normalize_backpack(vm_net_mock_role_state *role)
{
    vm_net_mock_backpack_item_state compact[VM_NET_MOCK_BACKPACK_MAX_ITEMS];
    u32 compactCount = 0;
    u32 occupiedSlots = 0;
    u32 declaredCount = 0;
    u16 maxSeq = 0;

    if (role == NULL)
        return;
    memset(compact, 0, sizeof(compact));
    if (role->backpackCapacity == 0)
        role->backpackCapacity = VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY;
    else if (role->backpackCapacity > VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT)
        role->backpackCapacity = VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT;
    declaredCount = role->backpackItemCount;
    if (declaredCount > VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        declaredCount = VM_NET_MOCK_BACKPACK_MAX_ITEMS;

    /*
     * Relational load writes rows into backpackItems[slot_index] from
     * account_role_backpack, while account_roles.backpack_item_count is only a
     * cached densified length.  Scanning only [0, declaredCount) drops any
     * occupied slot at/above that bound (including the common stale case
     * declaredCount==0 with leftover table rows), then the memset below would
     * wipe the just-loaded inventory for the whole login session.
     */
    for (u32 i = 0; i < VM_NET_MOCK_BACKPACK_MAX_ITEMS; ++i)
    {
        vm_net_mock_backpack_item_state item = role->backpackItems[i];
        if (item.itemId == 0 || item.count == 0)
            continue;
        ++occupiedSlots;
        if (compactCount >= role->backpackCapacity ||
            compactCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        {
            continue;
        }
        if (item.enhanceLevel > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
            item.enhanceLevel = VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL;
        if (item.seq == 0)
            item.seq = (u16)(maxSeq + 1);
        if (item.seq > maxSeq)
            maxSeq = item.seq;
        compact[compactCount++] = item;
    }
    if (occupiedSlots != declaredCount)
    {
        printf("[warn][network] mock_backpack_count_resync role=%u declared=%u occupied=%u kept=%u capacity=%u evidence=account_role_backpack-vs-backpack_item_count\n",
               role->roleId, declaredCount, occupiedSlots, compactCount,
               role->backpackCapacity);
        vm_autotest_note("mock_backpack_count_resync role=%u declared=%u occupied=%u kept=%u capacity=%u evidence=account_role_backpack-vs-backpack_item_count\n",
                         role->roleId, declaredCount, occupiedSlots, compactCount,
                         role->backpackCapacity);
    }
    memset(role->backpackItems, 0, sizeof(role->backpackItems));
    if (compactCount > 0)
        memcpy(role->backpackItems, compact, sizeof(compact[0]) * compactCount);
        role->backpackItemCount = (u16)compactCount;
    if (role->nextBackpackSeq == 0 || role->nextBackpackSeq <= maxSeq)
        role->nextBackpackSeq = (u16)(maxSeq + 1);
    if (role->nextBackpackSeq == 0)
        role->nextBackpackSeq = 1;
}

static void vm_net_mock_role_normalize(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    (void)vm_net_mock_role_refresh_special_designations(role);
    if (role->roleId == 0)
        role->roleId = VM_NET_MOCK_ROLE_DEFAULT_ID;
    role->name[sizeof(role->name) - 1] = 0;
    if (role->name[0] == 0)
        snprintf(role->name, sizeof(role->name), "%s", vm_net_mock_default_role_name());
    if (role->job == 0 || role->job > 3)
        role->job = 1;
    if (role->sex > 1)
        role->sex = 0;
    if (role->backpackCapacity == 0)
        role->backpackCapacity = VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY;
    else if (role->backpackCapacity > VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT)
        role->backpackCapacity = VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT;
    role->level = vm_net_mock_role_level_from_exp(role->exp);
    if (role->designationId >= VM_NET_MOCK_ROLE_DESIGNATION_COUNT)
        role->designationId = 0;
    if (!vm_net_mock_designation_is_unlocked(role, vm_net_mock_designation_by_id(role->designationId)))
        role->designationId = vm_net_mock_role_best_designation(role)->id;
    vm_net_mock_role_sync_derived_vitals(role);
    role->scene[sizeof(role->scene) - 1] = 0;
    if (!vm_net_mock_scene_name_is_persistable(role->scene))
        snprintf(role->scene, sizeof(role->scene), "%s", vm_net_mock_role_initial_scene_name());
    if (vm_net_mock_role_position_needs_edge_rescue(role->x, role->y))
    {
        if (!vm_net_mock_role_rescue_edge_position(role, "normalize-edge"))
        {
            role->x = VM_NET_MOCK_ROLE_INITIAL_X;
            role->y = VM_NET_MOCK_ROLE_INITIAL_Y;
            if (!vm_net_mock_scene_is_penglai01(role->scene))
            {
                (void)vm_net_mock_get_scene_reasonable_spawn_from_sce(role->scene,
                                                                      &role->x,
                                                                      &role->y,
                                                                      NULL);
            }
            vm_net_mock_adjust_safe_player_pos_for_scene(role->scene, &role->x,
                                                         &role->y);
        }
    }
    else
    {
        vm_net_mock_adjust_safe_player_pos_for_scene(role->scene, &role->x, &role->y);
        if (vm_net_mock_role_position_needs_edge_rescue(role->x, role->y))
            (void)vm_net_mock_role_rescue_edge_position(role, "normalize-after-portal");
    }
    vm_net_mock_role_normalize_backpack(role);
}

static bool vm_net_mock_role_is_pristine_bootstrap_default(const vm_net_mock_role_state *role)
{
    vm_net_mock_role_state expected;
    if (role == NULL)
        return false;
    vm_net_mock_role_init_default(&expected);
    return memcmp(role, &expected, sizeof(expected)) == 0;
}

static u32 vm_net_mock_role_db_repair_duplicate_default_names(void)
{
    bool seenDefaultName = false;
    u32 repairCount = 0;
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
        role->name[sizeof(role->name) - 1] = 0;
        if (!vm_net_mock_role_has_default_name(role))
            continue;
        if (!seenDefaultName)
        {
            seenDefaultName = true;
            continue;
        }
        vm_net_mock_role_assign_fallback_name(role);
        ++repairCount;
    }
    return repairCount;
}

static bool vm_net_mock_mysql_account_hex(char account_hex[129])
{
    const char *account_id = g_vm_mock_service_active_account_id;
    size_t account_len = vm_mock_mysql_bounded_strlen(account_id, 64);
    return account_id != NULL && account_len > 0 && account_len < 64 &&
           vm_mysql_hex_encode(account_id, account_len, account_hex, 129) != 0;
}

/*
 * Timed special effects have a different lifecycle from the role snapshot:
 * an item can expire while the character is offline, and an old binary role
 * file must not be reinterpreted as an active effect after a restart.  Keep
 * the record relational and keyed by the same account/role identity as the
 * backpack row it was consumed from.
 */
typedef struct
{
    vm_net_mock_role_item_effect effect;
    u32 pausedRemainingSec;
    bool found;
    bool invalid;
} vm_mock_mysql_role_item_effect_context;

typedef struct
{
    u32 count;
    bool found;
    bool invalid;
} vm_mock_mysql_item_effect_column_context;

static bool g_vm_net_mock_role_item_effect_schema_prepared = false;

static bool vm_net_mock_role_item_effect_is_valid(
    const vm_net_mock_role_item_effect *effect)
{
    if (effect == NULL || effect->itemId == 0 || effect->expiresUnix == 0)
        return false;
    if (effect->kind == VM_NET_MOCK_ROLE_ITEM_EFFECT_EXP_CARD)
    {
        return (effect->itemId == 809 && effect->multiplier == 2) ||
               (effect->itemId == 810 && effect->multiplier == 4) ||
               (effect->itemId == 811 && effect->multiplier == 10) ||
               (effect->itemId == 845 && effect->multiplier == 30);
    }
    if (effect->kind == VM_NET_MOCK_ROLE_ITEM_EFFECT_COMBAT_PILL)
    {
        /* Duration from item.dsh; ATK percent is the documented interpretation
         * of "??/????" stored in multiplier (829=30, 830=60). */
        return (effect->itemId == 829 && effect->multiplier == 30) ||
               (effect->itemId == 830 && effect->multiplier == 60);
    }
    if (effect->kind == VM_NET_MOCK_ROLE_ITEM_EFFECT_BATTLE_INSIGHT)
    {
        return effect->itemId == 828 && effect->multiplier == 20;
    }
    return false;
}

static bool vm_mock_mysql_item_effect_column_row(void *context_value,
                                                  unsigned int column_count,
                                                  const char *const *values,
                                                  const size_t *lengths)
{
    vm_mock_mysql_item_effect_column_context *context =
        (vm_mock_mysql_item_effect_column_context *)context_value;

    if (context == NULL || context->found || column_count != 1 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->count))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_net_mock_role_item_effect_ensure_paused_column(void)
{
    vm_mock_mysql_item_effect_column_context context;

    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(
            "SELECT COUNT(*) FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='account_role_item_effects' "
            "AND COLUMN_NAME='paused_remaining_sec'",
            vm_mock_mysql_item_effect_column_row, &context) ||
        context.invalid || !context.found)
    {
        printf("[error][mock-service] item_effect_paused_column probe error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    if (context.count != 0)
        return true;
    if (!vm_mysql_exec(
            "ALTER TABLE account_role_item_effects "
            "ADD COLUMN paused_remaining_sec INT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER expires_unix"))
    {
        printf("[error][mock-service] item_effect_paused_column alter error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    printf("[info][mock-service] item_effect_paused_column added "
           "evidence=exp-card-pause-while-offline\n");
    return true;
}

static bool vm_net_mock_role_prepare_item_effect_schema(void)
{
    if (g_vm_net_mock_role_item_effect_schema_prepared)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_role_item_effects ("
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "effect_kind TINYINT UNSIGNED NOT NULL,"
            "item_id INT UNSIGNED NOT NULL,"
            "multiplier TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "expires_unix INT UNSIGNED NOT NULL,"
            "paused_remaining_sec INT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(account_id,role_id,effect_kind),"
            "KEY idx_account_role_item_effects_expiry(expires_unix),"
            "CONSTRAINT fk_account_role_item_effects_role FOREIGN KEY(account_id,role_id) "
            "REFERENCES account_roles(account_id,role_id) ON DELETE CASCADE"
            ") ENGINE=InnoDB"))
    {
        printf("[error][mock-service] item_effect_schema_prepare error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    if (!vm_net_mock_role_item_effect_ensure_paused_column())
        return false;
    g_vm_net_mock_role_item_effect_schema_prepared = true;
    return true;
}

static bool vm_mock_mysql_role_item_effect_row(void *context_value,
                                                unsigned int column_count,
                                                const char *const *values,
                                                const size_t *lengths)
{
    vm_mock_mysql_role_item_effect_context *context =
        (vm_mock_mysql_role_item_effect_context *)context_value;
    u32 item_id = 0;
    u32 multiplier = 0;
    u32 expires_unix = 0;
    u32 paused_remaining_sec = 0;

    if (context == NULL || context->found || column_count != 4 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &item_id) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &multiplier) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &expires_unix) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &paused_remaining_sec) ||
        multiplier > 255)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->effect.itemId = item_id;
    context->effect.multiplier = multiplier;
    context->effect.expiresUnix = expires_unix;
    context->pausedRemainingSec = paused_remaining_sec;
    context->found = true;
    return true;
}

/* Returns false only for a storage or contract error.  A zero expiresUnix in
 * effectOut means that no currently active record exists.
 *
 * Exp cards and battle insight may be paused offline: paused_remaining_sec > 0
 * with expires_unix=0.  Readers see a synthetic expiresUnix = now + paused so
 * remaining UI / stack / battle bonus stay consistent until resume writes a
 * wall-clock end. */
static bool vm_net_mock_role_get_active_timed_item_effect(
    const vm_net_mock_role_state *role, u8 effect_kind,
    vm_net_mock_role_item_effect *effectOut)
{
    char account_hex[129];
    char query[768];
    vm_mock_mysql_role_item_effect_context context;
    u32 now = (u32)time(NULL);
    bool pausable = effect_kind == VM_NET_MOCK_ROLE_ITEM_EFFECT_EXP_CARD ||
                    effect_kind == VM_NET_MOCK_ROLE_ITEM_EFFECT_BATTLE_INSIGHT;

    if (effectOut)
        memset(effectOut, 0, sizeof(*effectOut));
    if (role == NULL || role->roleId == 0 || effect_kind == 0 ||
        !vm_net_mock_mysql_account_hex(account_hex) ||
        !vm_net_mock_role_prepare_item_effect_schema())
    {
        return false;
    }

    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT item_id,multiplier,expires_unix,paused_remaining_sec "
             "FROM account_role_item_effects "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND effect_kind=%u",
             account_hex, role->roleId, effect_kind);
    if (!vm_mysql_query(query, vm_mock_mysql_role_item_effect_row, &context) ||
        context.invalid)
    {
        return false;
    }
    if (!context.found)
        return true;

    context.effect.kind = effect_kind;
    if (pausable && context.pausedRemainingSec != 0)
    {
        if (now > 0xffffffffu - context.pausedRemainingSec)
            context.effect.expiresUnix = 0xffffffffu;
        else
            context.effect.expiresUnix = now + context.pausedRemainingSec;
    }
    else if (context.effect.expiresUnix <= now)
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM account_role_item_effects WHERE "
                 "account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND effect_kind=%u "
                 "AND expires_unix<=%u AND paused_remaining_sec=0",
                 account_hex, role->roleId, effect_kind, now);
        if (!vm_mysql_exec(query))
            return false;
        return true;
    }
    if (!vm_net_mock_role_item_effect_is_valid(&context.effect))
    {
        printf("[error][mock-service] item_effect_invalid account=%s role=%u kind=%u item=%u multiplier=%u expires=%u paused=%u\n",
               g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
               role->roleId, effect_kind, context.effect.itemId,
               context.effect.multiplier, context.effect.expiresUnix,
               context.pausedRemainingSec);
        return false;
    }
    if (effectOut)
        *effectOut = context.effect;
    return true;
}

typedef struct
{
    vm_net_mock_guild_record *rows;
    u32 rowCapacity;
    u32 rowCount;
    bool invalid;
} vm_mock_mysql_guild_rows_context;

typedef struct
{
    u32 value;
    bool found;
    bool invalid;
} vm_mock_mysql_guild_u32_context;

typedef struct
{
    vm_net_mock_guild_record guild;
    u8 rank;
    bool found;
    bool invalid;
} vm_mock_mysql_guild_membership_context;

typedef struct
{
    vm_net_mock_guild_member_record *rows;
    u32 rowCapacity;
    u32 rowCount;
    bool invalid;
} vm_mock_mysql_guild_member_rows_context;

typedef struct
{
    vm_net_mock_guild_application_record *rows;
    u32 rowCapacity;
    u32 rowCount;
    bool invalid;
} vm_mock_mysql_guild_application_rows_context;

static bool vm_net_mock_guild_mysql_query(const char *sql,
                                           vm_mysql_row_callback callback,
                                           void *context)
{
    if (vm_mysql_query(sql, callback, context))
        return true;
    /* This failure is raised before any request bytes reach MySQL, so no row
     * callback has run and replaying the read is safe.  Other receive/query
     * failures may have partially populated context and are not replayed. */
    if (strcmp(vm_mysql_last_error(), "MySQL socket send failed") != 0)
        return false;
    printf("[warn][network] mock_guild_mysql_reconnect reason=socket-send-failed\n");
    return vm_mysql_query(sql, callback, context);
}

static bool vm_net_mock_guild_decode_hex_text(const char *value,
                                               size_t valueLen,
                                               char *out,
                                               size_t outSize)
{
    size_t decodedLen = 0;
    if (out == NULL || outSize == 0 || value == NULL ||
        !vm_mysql_hex_decode(value, valueLen, out, outSize - 1, &decodedLen))
    {
        return false;
    }
    out[decodedLen] = 0;
    return true;
}

static void vm_net_mock_guild_limit_gbk_text(char *text, size_t maxBytes)
{
    size_t readPos = 0;
    size_t writeEnd = 0;
    size_t textLen = 0;
    if (text == NULL)
        return;
    textLen = strlen(text);
    while (readPos < textLen && readPos < maxBytes)
    {
        size_t charBytes = ((unsigned char)text[readPos] >= 0x80u) ? 2u : 1u;
        if (readPos + charBytes > textLen || readPos + charBytes > maxBytes)
            break;
        readPos += charBytes;
        writeEnd = readPos;
    }
    text[writeEnd] = 0;
}

static bool vm_mock_mysql_guild_u32_row(void *contextValue,
                                        unsigned int columnCount,
                                        const char *const *values,
                                        const size_t *lengths)
{
    vm_mock_mysql_guild_u32_context *context =
        (vm_mock_mysql_guild_u32_context *)contextValue;
    if (context == NULL || context->found || columnCount != 1 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->value))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_mock_mysql_guild_record_row(void *contextValue,
                                           unsigned int columnCount,
                                           const char *const *values,
                                           const size_t *lengths)
{
    vm_mock_mysql_guild_rows_context *context =
        (vm_mock_mysql_guild_rows_context *)contextValue;
    vm_net_mock_guild_record *guild = NULL;
    u32 values32[11];

    memset(values32, 0, sizeof(values32));
    if (context == NULL || context->rows == NULL ||
        context->rowCount >= context->rowCapacity || columnCount != 15)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &values32[0]) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &values32[1]) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &values32[2]) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &values32[3]) ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &values32[4]) ||
        !vm_mock_mysql_parse_u32(values[7], lengths[7], &values32[5]) ||
        !vm_mock_mysql_parse_u32(values[8], lengths[8], &values32[6]) ||
        !vm_mock_mysql_parse_u32(values[9], lengths[9], &values32[7]) ||
        !vm_mock_mysql_parse_u32(values[10], lengths[10], &values32[8]) ||
        !vm_mock_mysql_parse_u32(values[11], lengths[11], &values32[9]) ||
        !vm_mock_mysql_parse_u32(values[12], lengths[12], &values32[10]))
    {
        context->invalid = true;
        return true;
    }
    guild = &context->rows[context->rowCount];
    memset(guild, 0, sizeof(*guild));
    guild->guildId = values32[0];
    guild->guildLevel = values32[1];
    guild->minimumLevel = values32[2];
    guild->memberLimit = values32[3];
    guild->memberCount = values32[4];
    guild->guildMoney = values32[5];
    guild->prosperity = values32[6];
    guild->actionPower = values32[7];
    guild->researchPower = values32[8];
    guild->construction = values32[9];
    if (!vm_net_mock_guild_decode_hex_text(values[5], lengths[5],
                                            guild->guildName, sizeof(guild->guildName)) ||
        !vm_net_mock_guild_decode_hex_text(values[6], lengths[6],
                                            guild->leaderName, sizeof(guild->leaderName)) ||
        !vm_net_mock_guild_decode_hex_text(values[13], lengths[13],
                                            guild->currentConstruction,
                                            sizeof(guild->currentConstruction)) ||
        !vm_net_mock_guild_decode_hex_text(values[14], lengths[14],
                                            guild->notice, sizeof(guild->notice)))
    {
        context->invalid = true;
        memset(guild, 0, sizeof(*guild));
        return true;
    }
    /* The list/detail client structs reserve 13 bytes for the guild name and
     * 15 bytes for the leader.  Keep the terminating NUL inside those slots
     * and never split a two-byte GBK code point. */
    vm_net_mock_guild_limit_gbk_text(guild->guildName, 12);
    vm_net_mock_guild_limit_gbk_text(guild->leaderName, 14);
    ++context->rowCount;
    return true;
}

static bool vm_mock_mysql_guild_membership_row(void *contextValue,
                                               unsigned int columnCount,
                                               const char *const *values,
                                               const size_t *lengths)
{
    vm_mock_mysql_guild_membership_context *context =
        (vm_mock_mysql_guild_membership_context *)contextValue;
    u32 guildId = 0;
    u32 rank = 0;
    if (context == NULL || context->found || columnCount != 4 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &guildId) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &rank) || rank > 255 ||
        !vm_net_mock_guild_decode_hex_text(values[1], lengths[1],
                                            context->guild.guildName,
                                            sizeof(context->guild.guildName)) ||
        !vm_net_mock_guild_decode_hex_text(values[2], lengths[2],
                                            context->guild.leaderName,
                                            sizeof(context->guild.leaderName)))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->guild.guildId = guildId;
    vm_net_mock_guild_limit_gbk_text(context->guild.guildName, 12);
    vm_net_mock_guild_limit_gbk_text(context->guild.leaderName, 14);
    context->rank = (u8)rank;
    context->found = true;
    return true;
}

static bool vm_mock_mysql_guild_member_record_row(void *contextValue,
                                                   unsigned int columnCount,
                                                   const char *const *values,
                                                   const size_t *lengths)
{
    vm_mock_mysql_guild_member_rows_context *context =
        (vm_mock_mysql_guild_member_rows_context *)contextValue;
    vm_net_mock_guild_member_record *member = NULL;
    u32 rank = 0;

    if (context == NULL || context->rows == NULL ||
        context->rowCount >= context->rowCapacity || columnCount != 6)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    member = &context->rows[context->rowCount];
    memset(member, 0, sizeof(*member));
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &member->roleId) ||
        !vm_net_mock_guild_decode_hex_text(values[1], lengths[1],
                                            member->roleName, sizeof(member->roleName)) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &rank) || rank == 0 || rank > 255 ||
        !vm_net_mock_guild_decode_hex_text(values[3], lengths[3],
                                            member->memberTitle, sizeof(member->memberTitle)) ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &member->level) ||
        !vm_mock_mysql_copy_text(member->accountId, sizeof(member->accountId),
                                 values[5], lengths[5]) || member->roleId == 0)
    {
        context->invalid = true;
        memset(member, 0, sizeof(*member));
        return true;
    }
    member->memberRank = (u8)rank;
    /* HandleFactionPlayerListResponse stores the two strings in 16-byte
     * slots at row+4 and row+24.  Keep the terminating NUL in each slot. */
    vm_net_mock_guild_limit_gbk_text(member->roleName, 15);
    vm_net_mock_guild_limit_gbk_text(member->memberTitle, 15);
    ++context->rowCount;
    return true;
}

static bool vm_mock_mysql_guild_application_record_row(void *contextValue,
                                                        unsigned int columnCount,
                                                        const char *const *values,
                                                        const size_t *lengths)
{
    vm_mock_mysql_guild_application_rows_context *context =
        (vm_mock_mysql_guild_application_rows_context *)contextValue;
    vm_net_mock_guild_application_record *application = NULL;
    u32 job = 0;
    u32 sex = 0;

    if (context == NULL || context->rows == NULL ||
        context->rowCount >= context->rowCapacity || columnCount != 6)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    application = &context->rows[context->rowCount];
    memset(application, 0, sizeof(*application));
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &application->roleId) ||
        !vm_net_mock_guild_decode_hex_text(values[1], lengths[1],
                                            application->roleName,
                                            sizeof(application->roleName)) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &application->level) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &job) || job > 255 ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &sex) || sex > 255 ||
        !vm_mock_mysql_copy_text(application->accountId, sizeof(application->accountId),
                                 values[5], lengths[5]) ||
        application->roleId == 0 || application->accountId[0] == 0)
    {
        context->invalid = true;
        memset(application, 0, sizeof(*application));
        return true;
    }
    application->job = (u8)job;
    application->sex = (u8)sex;
    /* HandleRoleListResponse copies the name into the 16-byte slot at row+4. */
    vm_net_mock_guild_limit_gbk_text(application->roleName, 15);
    ++context->rowCount;
    return true;
}

static bool vm_net_mock_guild_query_records(const char *whereClause,
                                            const char *tailClause,
                                            vm_net_mock_guild_record *rows,
                                            u32 rowCapacity,
                                            u32 *rowCountOut)
{
    char query[2048];
    vm_mock_mysql_guild_rows_context context;
    if (rowCountOut)
        *rowCountOut = 0;
    if (rows == NULL || rowCapacity == 0)
        return false;
    memset(&context, 0, sizeof(context));
    context.rows = rows;
    context.rowCapacity = rowCapacity;
    snprintf(query, sizeof(query),
             "SELECT g.guild_id,g.guild_level,g.minimum_level,g.member_limit,COUNT(gm.role_id),"
             "HEX(g.guild_name),HEX(g.leader_role_name),g.guild_money,g.prosperity,g.action_power,"
             "g.research_power,g.construction,0,HEX(g.current_construction),HEX(g.notice) "
             "FROM guilds g LEFT JOIN guild_members gm ON gm.guild_id=g.guild_id %s "
             "GROUP BY g.guild_id,g.guild_level,g.minimum_level,g.member_limit,g.guild_name,"
             "g.leader_role_name,g.guild_money,g.prosperity,g.action_power,g.research_power,"
             "g.construction,g.current_construction,g.notice ORDER BY g.guild_id %s",
             whereClause ? whereClause : "",
             tailClause ? tailClause : "");
    if (!vm_net_mock_guild_mysql_query(query, vm_mock_mysql_guild_record_row, &context) || context.invalid)
    {
        printf("[error][network] mock_guild_query_failed error=%s\n", vm_mysql_last_error());
        return false;
    }
    if (rowCountOut)
        *rowCountOut = context.rowCount;
    return true;
}

static bool vm_net_mock_guild_query_members(u32 guildId,
                                             u32 offset,
                                             u32 pageSize,
                                             vm_net_mock_guild_member_record *rows,
                                             u32 rowCapacity,
                                             u32 *rowCountOut)
{
    char query[2048];
    vm_mock_mysql_guild_member_rows_context context;

    if (rowCountOut)
        *rowCountOut = 0;
    if (guildId == 0 || rows == NULL || rowCapacity == 0 || pageSize == 0)
        return false;
    if (pageSize > rowCapacity)
        pageSize = rowCapacity;
    memset(&context, 0, sizeof(context));
    context.rows = rows;
    context.rowCapacity = rowCapacity;
    snprintf(query, sizeof(query),
             "SELECT gm.role_id,HEX(gm.role_name),gm.member_rank,HEX(gm.member_title),"
             "ar.level,gm.account_id FROM guild_members gm "
             "JOIN account_roles ar ON ar.account_id=gm.account_id AND ar.role_id=gm.role_id "
             "WHERE gm.guild_id=%u ORDER BY gm.member_rank,gm.joined_at,gm.role_id "
             "LIMIT %u OFFSET %u",
             guildId, pageSize, offset);
    if (!vm_net_mock_guild_mysql_query(query, vm_mock_mysql_guild_member_record_row, &context) || context.invalid)
    {
        printf("[error][network] mock_guild_member_query_failed guild=%u error=%s\n",
               guildId, vm_mysql_last_error());
        return false;
    }
    if (rowCountOut)
        *rowCountOut = context.rowCount;
    return true;
}

static bool vm_net_mock_guild_find_member(u32 guildId, u32 roleId,
                                           vm_net_mock_guild_member_record *memberOut)
{
    char query[1536];
    vm_mock_mysql_guild_member_rows_context context;
    vm_net_mock_guild_member_record member;

    if (memberOut)
        memset(memberOut, 0, sizeof(*memberOut));
    if (guildId == 0 || roleId == 0)
        return false;
    memset(&context, 0, sizeof(context));
    memset(&member, 0, sizeof(member));
    context.rows = &member;
    context.rowCapacity = 1;
    snprintf(query, sizeof(query),
             "SELECT gm.role_id,HEX(gm.role_name),gm.member_rank,HEX(gm.member_title),"
             "ar.level,gm.account_id FROM guild_members gm "
             "JOIN account_roles ar ON ar.account_id=gm.account_id AND ar.role_id=gm.role_id "
             "WHERE gm.guild_id=%u AND gm.role_id=%u LIMIT 1",
             guildId, roleId);
    if (!vm_net_mock_guild_mysql_query(query, vm_mock_mysql_guild_member_record_row,
                                        &context) || context.invalid || context.rowCount != 1)
    {
        return false;
    }
    if (memberOut)
        *memberOut = member;
    return true;
}

static bool vm_net_mock_guild_query_applications(
    u32 guildId, u32 offset, u32 pageSize,
    vm_net_mock_guild_application_record *rows, u32 rowCapacity,
    u32 *rowCountOut)
{
    char query[1536];
    vm_mock_mysql_guild_application_rows_context context;

    if (rowCountOut)
        *rowCountOut = 0;
    if (guildId == 0 || rows == NULL || rowCapacity == 0 || pageSize == 0)
        return false;
    if (pageSize > rowCapacity)
        pageSize = rowCapacity;
    memset(&context, 0, sizeof(context));
    context.rows = rows;
    context.rowCapacity = rowCapacity;
    snprintf(query, sizeof(query),
             "SELECT applicant_role_id,HEX(applicant_role_name),applicant_level,"
             "applicant_job,applicant_sex,applicant_account_id "
             "FROM guild_applications WHERE guild_id=%u AND status=0 "
             "ORDER BY created_at,applicant_role_id LIMIT %u OFFSET %u",
             guildId, pageSize, offset);
    if (!vm_net_mock_guild_mysql_query(query,
                                        vm_mock_mysql_guild_application_record_row,
                                        &context) || context.invalid)
    {
        printf("[error][network] mock_guild_application_query_failed guild=%u error=%s\n",
               guildId, vm_mysql_last_error());
        return false;
    }
    if (rowCountOut)
        *rowCountOut = context.rowCount;
    return true;
}

static bool vm_net_mock_guild_find_pending_application(
    u32 guildId, u32 roleId, vm_net_mock_guild_application_record *applicationOut)
{
    char query[1536];
    vm_mock_mysql_guild_application_rows_context context;
    vm_net_mock_guild_application_record application;

    if (applicationOut)
        memset(applicationOut, 0, sizeof(*applicationOut));
    if (guildId == 0 || roleId == 0)
        return false;
    memset(&context, 0, sizeof(context));
    memset(&application, 0, sizeof(application));
    context.rows = &application;
    context.rowCapacity = 1;
    snprintf(query, sizeof(query),
             "SELECT applicant_role_id,HEX(applicant_role_name),applicant_level,"
             "applicant_job,applicant_sex,applicant_account_id "
             "FROM guild_applications WHERE guild_id=%u AND applicant_role_id=%u "
             "AND status=0 LIMIT 1",
             guildId, roleId);
    if (!vm_net_mock_guild_mysql_query(query,
                                        vm_mock_mysql_guild_application_record_row,
                                        &context) || context.invalid || context.rowCount != 1)
    {
        return false;
    }
    if (applicationOut)
        *applicationOut = application;
    return true;
}

static bool vm_net_mock_guild_count(const char *tableAndWhere, u32 *countOut)
{
    char query[768];
    vm_mock_mysql_guild_u32_context context;
    if (countOut)
        *countOut = 0;
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query), "SELECT COUNT(*) FROM %s",
             tableAndWhere ? tableAndWhere : "guilds");
    if (!vm_net_mock_guild_mysql_query(query, vm_mock_mysql_guild_u32_row, &context) ||
        context.invalid || !context.found)
    {
        return false;
    }
    if (countOut)
        *countOut = context.value;
    return true;
}

static bool vm_net_mock_guild_find_by_id(u32 guildId, vm_net_mock_guild_record *guildOut)
{
    char suffix[128];
    vm_net_mock_guild_record guild;
    u32 rowCount = 0;
    if (guildOut)
        memset(guildOut, 0, sizeof(*guildOut));
    if (guildId == 0)
        return false;
    memset(&guild, 0, sizeof(guild));
    snprintf(suffix, sizeof(suffix), "WHERE g.guild_id=%u", guildId);
    if (!vm_net_mock_guild_query_records(suffix, NULL, &guild, 1, &rowCount) || rowCount != 1)
        return false;
    if (guildOut)
        *guildOut = guild;
    return true;
}

static bool vm_net_mock_guild_find_role_membership(u32 roleId,
                                                    vm_net_mock_guild_record *guildOut,
                                                    u8 *rankOut)
{
    return vm_net_mock_guild_find_membership_for_account(g_vm_mock_service_active_account_id,
                                                          roleId, guildOut, rankOut);
}

static bool vm_net_mock_guild_find_membership_for_account(const char *accountId,
                                                           u32 roleId,
                                                           vm_net_mock_guild_record *guildOut,
                                                           u8 *rankOut)
{
    char accountHex[129];
    char query[1024];
    vm_mock_mysql_guild_membership_context context;
    if (guildOut)
        memset(guildOut, 0, sizeof(*guildOut));
    if (rankOut)
        *rankOut = 0;
    if (roleId == 0 || accountId == NULL || accountId[0] == 0 ||
        vm_mysql_hex_encode(accountId,
                            vm_mock_mysql_bounded_strlen(accountId, 64),
                            accountHex, sizeof(accountHex)) == 0)
        return false;
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT g.guild_id,HEX(g.guild_name),HEX(g.leader_role_name),gm.member_rank "
             "FROM guild_members gm JOIN guilds g ON g.guild_id=gm.guild_id "
             "WHERE gm.account_id=CAST(X'%s' AS CHAR) AND gm.role_id=%u LIMIT 1",
             accountHex, roleId);
    if (!vm_net_mock_guild_mysql_query(query, vm_mock_mysql_guild_membership_row, &context) ||
        context.invalid || !context.found)
    {
        return false;
    }
    if (guildOut)
        *guildOut = context.guild;
    if (rankOut)
        *rankOut = context.rank;
    return true;
}

typedef struct
{
    vm_net_mock_role_db_file *database;
    bool found;
    bool invalid;
    bool upgradedFormat;
    u8 seenRoleMask;
    u8 roleRows;
} vm_mock_mysql_role_load_context;

static bool vm_mock_mysql_role_meta_row(void *context_value,
                                        unsigned int column_count,
                                        const char *const *values,
                                        const size_t *lengths)
{
    vm_mock_mysql_role_load_context *context = (vm_mock_mysql_role_load_context *)context_value;
    u32 format_version = 0;
    u32 active_role_id = 0;
    u32 role_count = 0;
    if (context == NULL || context->database == NULL || context->found || column_count != 3 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &format_version) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &active_role_id) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &role_count) ||
        (format_version != (u32)VM_NET_MOCK_ROLE_DB_PRE_COSMETICS_VERSION &&
         format_version != (u32)VM_NET_MOCK_ROLE_DB_VERSION) ||
        role_count > VM_NET_MOCK_ROLE_DB_MAX_ROLES)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    memcpy(context->database->magic, "JHR1", 4);
    /* Cosmetics live in MySQL columns; bump in-memory/version marker to current. */
    context->database->version = VM_NET_MOCK_ROLE_DB_VERSION;
    context->database->activeRoleId = active_role_id;
    context->database->roleCount = role_count;
    if (format_version != (u32)VM_NET_MOCK_ROLE_DB_VERSION)
    {
        context->upgradedFormat = true;
        printf("[info][network] mock_role_db_mysql_meta_upgrade account_format=%u -> %u "
               "roles=%u active=%u\n",
               format_version, (u32)VM_NET_MOCK_ROLE_DB_VERSION, role_count,
               active_role_id);
    }
    context->found = true;
    return true;
}

static vm_net_mock_role_state *vm_mock_mysql_find_loaded_role(vm_net_mock_role_db_file *database,
                                                               u32 role_id);

typedef struct
{
    vm_net_mock_role_db_file *database;
    bool invalid;
    u32 rows;
} vm_mock_mysql_cosmetics_load_context;

typedef struct
{
    vm_net_mock_role_db_file *database;
    bool invalid;
} vm_mock_mysql_spouse_load_context;

static bool vm_mock_mysql_role_spouse_row(void *context_value,
                                          unsigned int column_count,
                                          const char *const *values,
                                          const size_t *lengths)
{
    vm_mock_mysql_spouse_load_context *context =
        (vm_mock_mysql_spouse_load_context *)context_value;
    u32 roleId = 0;
    u32 spouseRoleId = 0;
    vm_net_mock_role_state *role;
    size_t name_len = 0;
    size_t acct_len = 0;

    if (context == NULL || context->database == NULL || column_count != 4 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &roleId) || roleId == 0)
    {
        if (context)
            context->invalid = true;
        return true;
    }
    role = vm_mock_mysql_find_loaded_role(context->database, roleId);
    if (role == NULL)
    {
        context->invalid = true;
        return true;
    }
    role->spouseRoleId = 0;
    role->spouseAccountId[0] = 0;
    role->spouseName[0] = 0;
    (void)vm_mock_mysql_parse_u32(values[1], lengths[1], &spouseRoleId);
    role->spouseRoleId = spouseRoleId;
    if (values[2] != NULL && lengths[2] > 0)
    {
        acct_len = lengths[2] < sizeof(role->spouseAccountId) - 1
                       ? lengths[2]
                       : sizeof(role->spouseAccountId) - 1;
        memcpy(role->spouseAccountId, values[2], acct_len);
        role->spouseAccountId[acct_len] = 0;
    }
    if (values[3] != NULL && lengths[3] > 0)
    {
        if (!vm_mysql_hex_decode(values[3], lengths[3], role->spouseName,
                                 sizeof(role->spouseName) - 1, &name_len))
            role->spouseName[0] = 0;
        else
            role->spouseName[name_len] = 0;
    }
    return true;
}

static bool vm_mock_mysql_role_cosmetics_row(void *context_value,
                                             unsigned int column_count,
                                             const char *const *values,
                                             const size_t *lengths)
{
    vm_mock_mysql_cosmetics_load_context *context =
        (vm_mock_mysql_cosmetics_load_context *)context_value;
    u32 roleId = 0;
    u32 eqC = 0;
    u32 eqW = 0;
    u32 eqM = 0;
    vm_net_mock_role_state *role;
    size_t copy;

    if (context == NULL || context->database == NULL || column_count != 7 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &roleId) || roleId == 0)
    {
        if (context)
            context->invalid = true;
        return true;
    }
    role = vm_mock_mysql_find_loaded_role(context->database, roleId);
    if (role == NULL)
    {
        context->invalid = true;
        return true;
    }
    role->unlockedChenghaoIds[0] = 0;
    role->unlockedWingIds[0] = 0;
    role->unlockedMountIds[0] = 0;
    if (values[1] != NULL && lengths[1] > 0)
    {
        copy = lengths[1] < sizeof(role->unlockedChenghaoIds) - 1
                   ? lengths[1]
                   : sizeof(role->unlockedChenghaoIds) - 1;
        memcpy(role->unlockedChenghaoIds, values[1], copy);
        role->unlockedChenghaoIds[copy] = 0;
    }
    if (values[2] != NULL && lengths[2] > 0)
    {
        copy = lengths[2] < sizeof(role->unlockedWingIds) - 1
                   ? lengths[2]
                   : sizeof(role->unlockedWingIds) - 1;
        memcpy(role->unlockedWingIds, values[2], copy);
        role->unlockedWingIds[copy] = 0;
    }
    if (values[3] != NULL && lengths[3] > 0)
    {
        copy = lengths[3] < sizeof(role->unlockedMountIds) - 1
                   ? lengths[3]
                   : sizeof(role->unlockedMountIds) - 1;
        memcpy(role->unlockedMountIds, values[3], copy);
        role->unlockedMountIds[copy] = 0;
    }
    (void)vm_mock_mysql_parse_u32(values[4], lengths[4], &eqC);
    (void)vm_mock_mysql_parse_u32(values[5], lengths[5], &eqW);
    (void)vm_mock_mysql_parse_u32(values[6], lengths[6], &eqM);
    role->equippedChenghaoId = eqC;
    role->equippedWingId = eqW;
    role->equippedMountId = eqM;
    ++context->rows;
    return true;
}

static bool vm_mock_mysql_role_detail_row(void *context_value,
                                          unsigned int column_count,
                                          const char *const *values,
                                          const size_t *lengths)
{
    vm_mock_mysql_role_load_context *context = (vm_mock_mysql_role_load_context *)context_value;
    u32 number[23];
    size_t name_len = 0;
    size_t scene_len = 0;
    memset(number, 0, sizeof(number));
    if (context == NULL || context->database == NULL || column_count != 25 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &number[0]) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &number[1]) ||
        values[2] == NULL ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &number[2]) ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &number[3]) ||
        !vm_mock_mysql_parse_u32(values[5], lengths[5], &number[4]) ||
        !vm_mock_mysql_parse_u32(values[6], lengths[6], &number[5]) ||
        !vm_mock_mysql_parse_u32(values[7], lengths[7], &number[6]) ||
        !vm_mock_mysql_parse_u32(values[8], lengths[8], &number[7]) ||
        !vm_mock_mysql_parse_u32(values[9], lengths[9], &number[8]) ||
        !vm_mock_mysql_parse_u32(values[10], lengths[10], &number[9]) ||
        !vm_mock_mysql_parse_u32(values[11], lengths[11], &number[10]) ||
        !vm_mock_mysql_parse_u32(values[12], lengths[12], &number[11]) ||
        !vm_mock_mysql_parse_u32(values[13], lengths[13], &number[12]) ||
        values[14] == NULL ||
        !vm_mock_mysql_parse_u32(values[15], lengths[15], &number[13]) ||
        !vm_mock_mysql_parse_u32(values[16], lengths[16], &number[14]) ||
        !vm_mock_mysql_parse_u32(values[17], lengths[17], &number[15]) ||
        !vm_mock_mysql_parse_u32(values[18], lengths[18], &number[16]) ||
        !vm_mock_mysql_parse_u32(values[19], lengths[19], &number[17]) ||
        !vm_mock_mysql_parse_u32(values[20], lengths[20], &number[18]) ||
        !vm_mock_mysql_parse_u32(values[21], lengths[21], &number[19]) ||
        !vm_mock_mysql_parse_u32(values[22], lengths[22], &number[20]) ||
        !vm_mock_mysql_parse_u32(values[23], lengths[23], &number[21]) ||
        !vm_mock_mysql_parse_u32(values[24], lengths[24], &number[22]) ||
        number[0] >= context->database->roleCount || number[0] >= VM_NET_MOCK_ROLE_DB_MAX_ROLES ||
        number[1] == 0 || number[2] > 255 || number[3] > 255 ||
        number[4] > VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT ||
        number[13] > 65535 || number[14] > 65535 ||
        number[15] > VM_NET_MOCK_BACKPACK_MAX_ITEMS ||
        number[16] > 255 || number[19] > 65535 || number[21] > 255 ||
        (context->seenRoleMask & (1u << number[0])) != 0)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    vm_net_mock_role_state *role = &context->database->roles[number[0]];
    memset(role, 0, sizeof(*role));
    if (!vm_mysql_hex_decode(values[2], lengths[2], role->name,
                             sizeof(role->name) - 1, &name_len) ||
        !vm_mysql_hex_decode(values[14], lengths[14], role->scene,
                             sizeof(role->scene) - 1, &scene_len))
    {
        context->invalid = true;
        memset(role, 0, sizeof(*role));
        return true;
    }
    role->name[name_len] = 0;
    role->scene[scene_len] = 0;
    role->roleId = number[1];
    role->job = (u8)number[2];
    role->sex = (u8)number[3];
    role->backpackCapacity = (u16)number[4];
    role->level = number[5];
    role->exp = number[6];
    role->hp = number[7];
    role->hpMax = number[8];
    role->mp = number[9];
    role->mpMax = number[10];
    role->money = number[11];
    role->wcoin = number[12];
    role->x = (u16)number[13];
    role->y = (u16)number[14];
    role->backpackItemCount = (u16)number[15];
    role->designationId = (u8)number[16];
    role->arenaDesignationMask = number[17];
    role->specialDesignationMask = number[18];
    role->nextBackpackSeq = (u16)number[19];
    role->pkPoints = number[20];
    role->pkPveDecayStreak = (u8)number[21];
    role->createdAtUnix = number[22];
    context->seenRoleMask |= (u8)(1u << number[0]);
    ++context->roleRows;
    return true;
}

static vm_net_mock_role_state *vm_mock_mysql_find_loaded_role(vm_net_mock_role_db_file *database,
                                                               u32 role_id)
{
    if (database == NULL || role_id == 0)
        return NULL;
    for (u32 i = 0; i < database->roleCount; ++i)
    {
        if (database->roles[i].roleId == role_id)
            return &database->roles[i];
    }
    return NULL;
}

static bool vm_mock_mysql_role_equipment_row(void *context_value,
                                             unsigned int column_count,
                                             const char *const *values,
                                             const size_t *lengths)
{
    vm_mock_mysql_role_load_context *context = (vm_mock_mysql_role_load_context *)context_value;
    u32 role_id = 0;
    u32 slot_index = 0;
    u32 item_id = 0;
    u32 enhance_level = 0;
    if (context == NULL || context->database == NULL ||
        (column_count != 3 && column_count != 4 && column_count != 5 &&
         column_count != 6) ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &role_id) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &slot_index) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &item_id) ||
        slot_index >= VM_NET_MOCK_EQUIP_SLOT_COUNT)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    if (column_count >= 4)
    {
        if (!vm_mock_mysql_parse_u32(values[3], lengths[3], &enhance_level) ||
            enhance_level > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
        {
            context->invalid = true;
            return true;
        }
    }
    vm_net_mock_role_state *role = vm_mock_mysql_find_loaded_role(context->database, role_id);
    if (role == NULL)
    {
        context->invalid = true;
        return true;
    }
    role->equippedItemIds[slot_index] = item_id;
    role->equippedEnhanceLevels[slot_index] = (u16)enhance_level;
    role->equippedEnhanceInstanceIds[slot_index] = 0;
    vm_net_mock_enhance_wash_clear(&role->equippedEnhanceWash[slot_index]);
    if (column_count == 5 && values[4] != NULL)
        (void)vm_net_mock_enhance_wash_decode(values[4], lengths[4],
                                              &role->equippedEnhanceWash[slot_index]);
    if (column_count >= 6)
    {
        if (values[4] != NULL)
            (void)vm_mock_mysql_parse_u32(values[4], lengths[4],
                                          &role->equippedEnhanceInstanceIds[slot_index]);
        if (values[5] != NULL)
            (void)vm_net_mock_enhance_wash_decode(values[5], lengths[5],
                                                  &role->equippedEnhanceWash[slot_index]);
    }
    return true;
}

static bool vm_mock_mysql_role_backpack_row(void *context_value,
                                            unsigned int column_count,
                                            const char *const *values,
                                            const size_t *lengths)
{
    vm_mock_mysql_role_load_context *context = (vm_mock_mysql_role_load_context *)context_value;
    u32 role_id = 0;
    u32 slot_index = 0;
    u32 item_id = 0;
    u32 item_seq = 0;
    u32 item_count = 0;
    u32 enhance_level = 0;
    if (context == NULL || context->database == NULL ||
        (column_count != 6 && column_count != 7 && column_count != 8) ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &role_id) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &slot_index) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &item_id) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &item_seq) || item_seq > 65535 ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &item_count) ||
        !vm_mock_mysql_parse_u32(values[5], lengths[5], &enhance_level) ||
        enhance_level > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL ||
        slot_index >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    vm_net_mock_role_state *role = vm_mock_mysql_find_loaded_role(context->database, role_id);
    if (role == NULL)
    {
        context->invalid = true;
        return true;
    }
    role->backpackItems[slot_index].itemId = item_id;
    role->backpackItems[slot_index].seq = (u16)item_seq;
    role->backpackItems[slot_index].enhanceLevel = (u16)enhance_level;
    role->backpackItems[slot_index].enhanceInstanceId = 0;
    role->backpackItems[slot_index].count = item_count;
    vm_net_mock_enhance_wash_clear(&role->backpackItems[slot_index].enhanceWash);
    if (column_count == 7 && values[6] != NULL)
        (void)vm_net_mock_enhance_wash_decode(
            values[6], lengths[6], &role->backpackItems[slot_index].enhanceWash);
    if (column_count >= 8)
    {
        if (values[6] != NULL)
            (void)vm_mock_mysql_parse_u32(values[6], lengths[6],
                                          &role->backpackItems[slot_index].enhanceInstanceId);
        if (values[7] != NULL)
            (void)vm_net_mock_enhance_wash_decode(
                values[7], lengths[7], &role->backpackItems[slot_index].enhanceWash);
    }
    return true;
}

typedef struct
{
    u32 count;
    bool found;
    bool invalid;
} vm_mock_mysql_equipment_enhance_column_context;

static bool vm_mock_mysql_equipment_enhance_column_row(void *context_value,
                                                       unsigned int column_count,
                                                       const char *const *values,
                                                       const size_t *lengths)
{
    vm_mock_mysql_equipment_enhance_column_context *context =
        (vm_mock_mysql_equipment_enhance_column_context *)context_value;
    if (context == NULL || column_count != 1 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->count))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_net_mock_role_table_ensure_column(const char *tableName,
                                                 const char *columnName,
                                                 const char *alterSql,
                                                 const char *logTag)
{
    vm_mock_mysql_equipment_enhance_column_context context;
    char probe[320];

    if (tableName == NULL || columnName == NULL || alterSql == NULL)
        return false;
    memset(&context, 0, sizeof(context));
    snprintf(probe, sizeof(probe),
             "SELECT COUNT(*) FROM information_schema.COLUMNS "
             "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='%s' "
             "AND COLUMN_NAME='%s'",
             tableName, columnName);
    if (!vm_mysql_query(probe, vm_mock_mysql_equipment_enhance_column_row, &context) ||
        context.invalid || !context.found)
    {
        printf("[error][network] mock_%s probe error=%s\n",
               logTag != NULL ? logTag : "column", vm_mysql_last_error());
        return false;
    }
    if (context.count == 0)
    {
        if (!vm_mysql_exec(alterSql))
        {
            printf("[error][network] mock_%s alter error=%s\n",
                   logTag != NULL ? logTag : "column", vm_mysql_last_error());
            return false;
        }
        printf("[info][network] mock_%s added\n",
               logTag != NULL ? logTag : "column");
    }
    return true;
}

static bool vm_net_mock_role_equipment_ensure_enhance_column(void)
{
    static bool ensured = false;
    static bool ok = false;

    if (ensured)
        return ok;
    ensured = true;
    if (!vm_net_mock_role_table_ensure_column(
            "account_role_equipment", "enhance_level",
            "ALTER TABLE account_role_equipment "
            "ADD COLUMN enhance_level SMALLINT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER item_id",
            "equipment_enhance_column"))
    {
        ok = false;
        return false;
    }
    if (!vm_net_mock_role_table_ensure_column(
            "account_role_equipment", "enhance_wash",
            "ALTER TABLE account_role_equipment "
            "ADD COLUMN enhance_wash VARCHAR(96) NOT NULL DEFAULT "
            "'0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0' "
            "AFTER enhance_level",
            "equipment_enhance_wash_column"))
    {
        ok = false;
        return false;
    }
    if (!vm_net_mock_role_table_ensure_column(
            "account_role_backpack", "enhance_wash",
            "ALTER TABLE account_role_backpack "
            "ADD COLUMN enhance_wash VARCHAR(96) NOT NULL DEFAULT "
            "'0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0' "
            "AFTER enhance_level",
            "backpack_enhance_wash_column") ||
        !vm_net_mock_role_table_ensure_column(
            "account_role_equipment", "item_instance_id",
            "ALTER TABLE account_role_equipment "
            "ADD COLUMN item_instance_id INT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER enhance_level",
            "equipment_item_instance_id_column") ||
        !vm_net_mock_role_table_ensure_column(
            "account_role_backpack", "item_instance_id",
            "ALTER TABLE account_role_backpack "
            "ADD COLUMN item_instance_id INT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER enhance_level",
            "backpack_item_instance_id_column"))
    {
        ok = false;
        return false;
    }
    ok = true;
    return true;
}

/* account_roles.pk_points (ranking) + pk_pve_streak (red-name decay). */
static void vm_net_mock_role_mark_inventory_dirty(const char *reason);

/*
 * Legacy DBs kept backpack_capacity / backpack_item_count as TINYINT UNSIGNED
 * (max 255). Pixi cap is 256; leave them tinyint and load rejects capacity=256
 * as invalid → empty title role list. Widen once when still tinyint.
 */
static bool vm_net_mock_role_ensure_backpack_u16_columns(void)
{
    static bool ensured = false;
    static bool ok = false;
    vm_mock_mysql_equipment_enhance_column_context context;
    char probe[384];

    if (ensured)
        return ok;
    ensured = true;
    memset(&context, 0, sizeof(context));
    snprintf(probe, sizeof(probe),
             "SELECT COUNT(*) FROM information_schema.COLUMNS "
             "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='account_roles' "
             "AND COLUMN_NAME IN ('backpack_capacity','backpack_item_count') "
             "AND DATA_TYPE='tinyint'");
    if (!vm_mysql_query(probe, vm_mock_mysql_equipment_enhance_column_row, &context) ||
        context.invalid || !context.found)
    {
        printf("[error][network] mock_backpack_u16_columns probe error=%s\n",
               vm_mysql_last_error());
        ok = false;
        return false;
    }
    if (context.count > 0)
    {
        if (!vm_mysql_exec(
                "ALTER TABLE account_roles "
                "MODIFY COLUMN backpack_capacity SMALLINT UNSIGNED NOT NULL, "
                "MODIFY COLUMN backpack_item_count SMALLINT UNSIGNED NOT NULL"))
        {
            printf("[error][network] mock_backpack_u16_columns alter error=%s\n",
                   vm_mysql_last_error());
            ok = false;
            return false;
        }
        printf("[info][network] mock_backpack_u16_columns widened "
               "tinyint_cols=%u evidence=capacity-256\n",
               context.count);
    }
    ok = true;
    return true;
}

static bool vm_net_mock_role_ensure_pk_columns(void)
{
    static bool ensured = false;
    static bool ok = false;

    if (ensured)
        return ok;
    ensured = true;
    if (!vm_net_mock_role_ensure_backpack_u16_columns())
    {
        ok = false;
        return false;
    }
    if (!vm_net_mock_role_table_ensure_column(
            "account_roles", "pk_points",
            "ALTER TABLE account_roles "
            "ADD COLUMN pk_points INT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER wcoin",
            "role_pk_points_column"))
    {
        ok = false;
        return false;
    }
    if (!vm_net_mock_role_table_ensure_column(
            "account_roles", "pk_pve_streak",
            "ALTER TABLE account_roles "
            "ADD COLUMN pk_pve_streak TINYINT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER pk_points",
            "role_pk_pve_streak_column"))
    {
        ok = false;
        return false;
    }
    ok = true;
    return true;
}

static bool vm_net_mock_role_ensure_arena_designation_column(void)
{
    static bool ensured = false;
    static bool ok = false;

    if (ensured)
        return ok;
    ensured = true;
    if (!vm_net_mock_role_ensure_pk_columns())
    {
        ok = false;
        return false;
    }
    if (!vm_net_mock_role_table_ensure_column(
            "account_roles", "arena_designation_mask",
            "ALTER TABLE account_roles "
            "ADD COLUMN arena_designation_mask INT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER designation_id",
            "role_arena_designation_mask_column"))
    {
        ok = false;
        return false;
    }
    if (!vm_net_mock_role_table_ensure_column(
            "account_roles", "special_designation_mask",
            "ALTER TABLE account_roles "
            "ADD COLUMN special_designation_mask INT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER arena_designation_mask",
            "role_special_designation_mask_column"))
    {
        ok = false;
        return false;
    }
    ok = true;
    return true;
}

static bool vm_net_mock_role_ensure_pixi_cosmetics_columns(void)
{
    static bool ensured = false;
    static bool ok = false;

    if (ensured)
        return ok;
    ensured = true;
    if (!vm_net_mock_role_ensure_arena_designation_column())
    {
        ok = false;
        return false;
    }
    if (!vm_net_mock_role_table_ensure_column(
            "account_roles", "unlocked_chenghao_ids",
            "ALTER TABLE account_roles "
            "ADD COLUMN unlocked_chenghao_ids TEXT NULL "
            "AFTER special_designation_mask",
            "role_unlocked_chenghao_ids_column") ||
        !vm_net_mock_role_table_ensure_column(
            "account_roles", "unlocked_wing_ids",
            "ALTER TABLE account_roles "
            "ADD COLUMN unlocked_wing_ids TEXT NULL "
            "AFTER unlocked_chenghao_ids",
            "role_unlocked_wing_ids_column") ||
        !vm_net_mock_role_table_ensure_column(
            "account_roles", "unlocked_mount_ids",
            "ALTER TABLE account_roles "
            "ADD COLUMN unlocked_mount_ids TEXT NULL "
            "AFTER unlocked_wing_ids",
            "role_unlocked_mount_ids_column") ||
        !vm_net_mock_role_table_ensure_column(
            "account_roles", "equipped_chenghao_id",
            "ALTER TABLE account_roles "
            "ADD COLUMN equipped_chenghao_id INT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER unlocked_mount_ids",
            "role_equipped_chenghao_id_column") ||
        !vm_net_mock_role_table_ensure_column(
            "account_roles", "equipped_wing_id",
            "ALTER TABLE account_roles "
            "ADD COLUMN equipped_wing_id INT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER equipped_chenghao_id",
            "role_equipped_wing_id_column") ||
        !vm_net_mock_role_table_ensure_column(
            "account_roles", "equipped_mount_id",
            "ALTER TABLE account_roles "
            "ADD COLUMN equipped_mount_id INT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER equipped_wing_id",
            "role_equipped_mount_id_column"))
    {
        ok = false;
        return false;
    }
    ok = true;
    return true;
}

static bool vm_net_mock_role_ensure_intimacy_marriage_columns(void)
{
    static bool ensured = false;
    static bool ok = false;

    if (ensured)
        return ok;
    ensured = true;
    if (!vm_net_mock_role_ensure_pixi_cosmetics_columns())
    {
        ok = false;
        return false;
    }
    if (!vm_net_mock_role_table_ensure_column(
            "friendships", "last_team_at",
            "ALTER TABLE friendships "
            "ADD COLUMN last_team_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "AFTER target_sex",
            "friendships_last_team_at_column") ||
        !vm_net_mock_role_table_ensure_column(
            "friendships", "last_decay_at",
            "ALTER TABLE friendships "
            "ADD COLUMN last_decay_at TIMESTAMP NULL DEFAULT NULL "
            "AFTER last_team_at",
            "friendships_last_decay_at_column") ||
        !vm_net_mock_role_table_ensure_column(
            "account_roles", "spouse_role_id",
            "ALTER TABLE account_roles "
            "ADD COLUMN spouse_role_id INT UNSIGNED NOT NULL DEFAULT 0 "
            "AFTER equipped_mount_id",
            "role_spouse_role_id_column") ||
        !vm_net_mock_role_table_ensure_column(
            "account_roles", "spouse_account_id",
            "ALTER TABLE account_roles "
            "ADD COLUMN spouse_account_id VARCHAR(63) CHARACTER SET ascii "
            "COLLATE ascii_bin NOT NULL DEFAULT '' "
            "AFTER spouse_role_id",
            "role_spouse_account_id_column") ||
        !vm_net_mock_role_table_ensure_column(
            "account_roles", "spouse_name",
            "ALTER TABLE account_roles "
            "ADD COLUMN spouse_name VARBINARY(32) NOT NULL DEFAULT '' "
            "AFTER spouse_account_id",
            "role_spouse_name_column"))
    {
        ok = false;
        return false;
    }
    ok = true;
    return true;
}

static bool vm_net_mock_role_is_red_name(const vm_net_mock_role_state *role)
{
    return role != NULL &&
           role->pkPoints >= (u32)VM_NET_MOCK_PK_RED_THRESHOLD;
}

static void vm_net_mock_role_note_pve_victory_pk_decay(vm_net_mock_role_state *role)
{
    u32 before;

    if (role == NULL || role->pkPoints == 0)
        return;
    before = role->pkPoints;
    role->pkPveDecayStreak =
        (u8)(role->pkPveDecayStreak + 1u);
    if (role->pkPveDecayStreak < (u8)VM_NET_MOCK_PK_PVE_DECAY_BATTLES)
    {
        printf("[info][network] mock_pk_pve_decay_progress role=%u pk=%u "
               "streak=%u/%u\n",
               role->roleId, role->pkPoints, role->pkPveDecayStreak,
               (u32)VM_NET_MOCK_PK_PVE_DECAY_BATTLES);
        vm_net_mock_role_mark_inventory_dirty("pk-pve-streak");
        return;
    }
    role->pkPveDecayStreak = 0;
    role->pkPoints -= 1u;
    printf("[info][network] mock_pk_pve_decay role=%u pk=%u->%u red=%u "
           "evidence=3-pve-wins\n",
           role->roleId, before, role->pkPoints,
           vm_net_mock_role_is_red_name(role) ? 1u : 0u);
    vm_net_mock_role_mark_inventory_dirty("pk-pve-decay");
}

static void vm_net_mock_role_add_pk_initiate_points(vm_net_mock_role_state *role,
                                                     const char *reason)
{
    u32 before;
    u32 gain = (u32)VM_NET_MOCK_PK_INITIATE_GAIN;
    u32 cap = (u32)VM_NET_MOCK_PK_POINTS_MAX;

    if (role == NULL || gain == 0)
        return;
    before = role->pkPoints;
    if (role->pkPoints >= cap)
        return;
    if (role->pkPoints > cap - gain)
        role->pkPoints = cap;
    else
        role->pkPoints += gain;
    printf("[info][network] mock_pk_initiate_gain role=%u pk=%u->%u gain=%u "
           "cap=%u red=%u reason=%s\n",
           role->roleId, before, role->pkPoints, role->pkPoints - before, cap,
           vm_net_mock_role_is_red_name(role) ? 1u : 0u,
           reason ? reason : "spar-initiate");
    vm_net_mock_role_mark_inventory_dirty(reason ? reason : "spar-initiate-pk");
}

static bool vm_net_mock_warehouse_ensure_enhance_wash_column(void)
{
    static bool ensured = false;
    static bool ok = false;

    if (ensured)
        return ok;
    ensured = true;
    ok = vm_net_mock_role_table_ensure_column(
        "account_role_warehouse", "enhance_wash",
        "ALTER TABLE account_role_warehouse "
        "ADD COLUMN enhance_wash VARCHAR(96) NOT NULL DEFAULT "
        "'0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0' "
        "AFTER enhance_level",
        "warehouse_enhance_wash_column");
    return ok;
}

static bool vm_net_mock_enhance_wash_sql_text(
    const vm_net_mock_enhance_wash_state *wash, char *out, size_t outCap)
{
    size_t i;
    if (out == NULL || outCap < 8)
        return false;
    if (!vm_net_mock_enhance_wash_encode(wash, out, outCap) || out[0] == 0)
    {
        snprintf(out, outCap, "0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0");
    }
    for (i = 0; out[i] != 0; ++i)
    {
        char c = out[i];
        if (!((c >= '0' && c <= '9') || c == ',' || c == ';'))
        {
            snprintf(out, outCap, "0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0");
            return false;
        }
    }
    return true;
}

static bool g_vm_net_mock_role_db_format_upgraded;

static void vm_net_mock_role_finalize_loaded_enhance_instances(
    vm_net_mock_role_state *role);

static bool vm_net_mock_role_db_load_mysql_relational(bool *found_out)
{
    char account_hex[129];
    char query[768];
    vm_mock_mysql_role_load_context context;
    if (found_out)
        *found_out = false;
    g_vm_net_mock_role_db_format_upgraded = false;
    if (!vm_net_mock_mysql_account_hex(account_hex))
        return false;
    memset(&context, 0, sizeof(context));
    context.database = &g_vm_net_mock_role_db;
    snprintf(query, sizeof(query),
             "SELECT format_version,active_role_id,role_count FROM account_role_state WHERE account_id=CAST(X'%s' AS CHAR)",
             account_hex);
    if (!vm_mysql_query(query, vm_mock_mysql_role_meta_row, &context))
        return false;
    if (context.invalid || !context.found)
    {
        if (found_out)
            *found_out = context.found;
        return !context.invalid;
    }
    g_vm_net_mock_role_db_format_upgraded = context.upgradedFormat;
    if (!vm_net_mock_role_ensure_pk_columns())
        return false;
    if (!vm_net_mock_role_ensure_arena_designation_column())
        return false;
    if (!vm_net_mock_role_ensure_pixi_cosmetics_columns())
        return false;
    snprintf(query, sizeof(query),
             "SELECT role_index,role_id,HEX(role_name),job,sex,backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,HEX(scene),pos_x,pos_y,backpack_item_count,designation_id,arena_designation_mask,special_designation_mask,next_backpack_seq,pk_points,pk_pve_streak,UNIX_TIMESTAMP(created_at) "
             "FROM account_roles WHERE account_id=CAST(X'%s' AS CHAR) ORDER BY role_index",
             account_hex);
    if (!vm_mysql_query(query, vm_mock_mysql_role_detail_row, &context) || context.invalid ||
        context.roleRows != g_vm_net_mock_role_db.roleCount)
        return false;
    {
        vm_mock_mysql_cosmetics_load_context cosCtx;
        memset(&cosCtx, 0, sizeof(cosCtx));
        cosCtx.database = &g_vm_net_mock_role_db;
        snprintf(query, sizeof(query),
                 "SELECT role_id,IFNULL(unlocked_chenghao_ids,''),IFNULL(unlocked_wing_ids,''),"
                 "IFNULL(unlocked_mount_ids,''),equipped_chenghao_id,equipped_wing_id,"
                 "equipped_mount_id FROM account_roles WHERE account_id=CAST(X'%s' AS CHAR)",
                 account_hex);
        if (!vm_mysql_query(query, vm_mock_mysql_role_cosmetics_row, &cosCtx) ||
            cosCtx.invalid)
            return false;
    }
    if (!vm_net_mock_role_ensure_intimacy_marriage_columns())
        return false;
    {
        vm_mock_mysql_spouse_load_context spCtx;
        memset(&spCtx, 0, sizeof(spCtx));
        spCtx.database = &g_vm_net_mock_role_db;
        snprintf(query, sizeof(query),
                 "SELECT role_id,spouse_role_id,IFNULL(spouse_account_id,''),"
                 "HEX(IFNULL(spouse_name,'')) FROM account_roles "
                 "WHERE account_id=CAST(X'%s' AS CHAR)",
                 account_hex);
        if (!vm_mysql_query(query, vm_mock_mysql_role_spouse_row, &spCtx) ||
            spCtx.invalid)
            return false;
    }
    if (!vm_net_mock_role_equipment_ensure_enhance_column())
        return false;
    snprintf(query, sizeof(query),
             "SELECT role_id,slot_index,item_id,enhance_level,item_instance_id,enhance_wash FROM account_role_equipment WHERE account_id=CAST(X'%s' AS CHAR) ORDER BY role_id,slot_index",
             account_hex);
    if (!vm_mysql_query(query, vm_mock_mysql_role_equipment_row, &context) || context.invalid)
        return false;
    snprintf(query, sizeof(query),
             "SELECT role_id,slot_index,item_id,item_seq,item_count,enhance_level,item_instance_id,enhance_wash FROM account_role_backpack WHERE account_id=CAST(X'%s' AS CHAR) ORDER BY role_id,slot_index",
             account_hex);
    if (!vm_mysql_query(query, vm_mock_mysql_role_backpack_row, &context) || context.invalid)
        return false;
    for (u32 roleIndex = 0; roleIndex < context.database->roleCount; ++roleIndex)
        vm_net_mock_role_finalize_loaded_enhance_instances(
            &context.database->roles[roleIndex]);
    if (found_out)
        *found_out = true;
    return true;
}

typedef struct
{
    vm_net_mock_role_db_file *database;
    bool found;
    bool invalid;
} vm_mock_mysql_payload_load_context;

static bool vm_mock_mysql_role_payload_row(void *context_value,
                                           unsigned int column_count,
                                           const char *const *values,
                                           const size_t *lengths)
{
    vm_mock_mysql_payload_load_context *context = (vm_mock_mysql_payload_load_context *)context_value;
    u32 format_version = 0;
    u32 active_role_id = 0;
    u32 role_count = 0;
    size_t payload_len = 0;
    if (context == NULL || context->database == NULL || context->found || column_count != 4 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &format_version) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &active_role_id) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &role_count) || values[3] == NULL ||
        !vm_mysql_hex_decode(values[3], lengths[3], context->database,
                             sizeof(*context->database), &payload_len) ||
        payload_len != sizeof(*context->database) || format_version != VM_NET_MOCK_ROLE_DB_VERSION ||
        memcmp(context->database->magic, "JHR1", 4) != 0 ||
        context->database->version != format_version ||
        context->database->activeRoleId != active_role_id ||
        context->database->roleCount != role_count || role_count > VM_NET_MOCK_ROLE_DB_MAX_ROLES)
    {
        if (context)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_net_mock_role_db_load_mysql_payload_backup(bool *found_out)
{
    char account_hex[129];
    char query[512];
    vm_mock_mysql_payload_load_context context;
    if (found_out)
        *found_out = false;
    if (!vm_net_mock_mysql_account_hex(account_hex))
        return false;
    memset(&context, 0, sizeof(context));
    context.database = &g_vm_net_mock_role_db;
    snprintf(query, sizeof(query),
             "SELECT format_version,active_role_id,role_count,HEX(payload) FROM account_role_state_payload_backup WHERE account_id=CAST(X'%s' AS CHAR)",
             account_hex);
    if (!vm_mysql_query(query, vm_mock_mysql_role_payload_row, &context) || context.invalid)
        return false;
    if (found_out)
        *found_out = context.found;
    return true;
}

typedef struct
{
    u32 value;
    bool found;
    bool invalid;
} vm_mock_mysql_u32_context;

static bool vm_mock_mysql_single_u32_row(void *context_value,
                                         unsigned int column_count,
                                         const char *const *values,
                                         const size_t *lengths)
{
    vm_mock_mysql_u32_context *context = (vm_mock_mysql_u32_context *)context_value;
    if (context == NULL || context->found || column_count != 1 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->value))
    {
        if (context)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_net_mock_allocate_global_role_id(u32 *role_id_out)
{
    char account_hex[129];
    char query[384];
    vm_mock_mysql_u32_context context;
    if (role_id_out)
        *role_id_out = 0;
    if (!vm_net_mock_mysql_account_hex(account_hex))
        return false;
    snprintf(query, sizeof(query),
             "INSERT INTO role_id_sequence(account_id) VALUES(CAST(X'%s' AS CHAR))",
             account_hex);
    if (!vm_mysql_exec(query))
        return false;
    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query("SELECT LAST_INSERT_ID()", vm_mock_mysql_single_u32_row, &context) ||
        context.invalid || !context.found || context.value == 0)
        return false;
    if (role_id_out)
        *role_id_out = context.value;
    return true;
}

static void vm_net_mock_apply_role_id_migration_to_friend_cache(const char *account_id,
                                                                 const u32 *old_ids,
                                                                 const u32 *new_ids,
                                                                 u32 mapping_count)
{
    /* Friendships are read from MySQL; role_db_save_relational already UPDATEs
     * friendships.owner_role_id / target_role_id.  The in-memory cache is not
     * used after seal. */
    (void)account_id;
    (void)old_ids;
    (void)new_ids;
    (void)mapping_count;
}

/*
 * Player warehouse is relational state outside the binary role snapshot.
 * Declared before role_db_save so backpack + warehouse can share one
 * MySQL transaction on deposit/retrieve.
 */
static vm_net_mock_warehouse_state g_vm_net_mock_warehouse;
static bool g_vm_net_mock_warehouse_schema_prepared = false;
static bool vm_net_mock_warehouse_write_sql(const vm_net_mock_role_state *role,
                                            const char *account_hex,
                                            const vm_net_mock_warehouse_state *warehouse,
                                            char *mysql_error,
                                            size_t mysql_error_cap);

static bool vm_net_mock_mysql_account_hex_for(const char *accountId,
                                              char account_hex[129])
{
    size_t account_len = vm_mock_mysql_bounded_strlen(accountId, 64);
    return accountId != NULL && account_len > 0 && account_len < 64 &&
           vm_mysql_hex_encode(accountId, account_len, account_hex, 129) != 0;
}

static bool vm_net_mock_role_db_save_relational_ex(
    const char *reason,
    const char *account_id,
    vm_net_mock_role_db_file *db,
    bool db_valid,
    vm_net_mock_warehouse_state *warehouse,
    bool clear_process_dirty_flags,
    const u32 *old_ids,
    const u32 *new_ids,
    u32 mapping_count,
    bool full_snapshot,
    u32 scope_role_id,
    const vm_net_mock_role_item_effect *timed_effect)
{
    char account_hex[129];
    char query[3072];
    char mysql_error[512];
    char *bulk_query = NULL;
    size_t bulk_capacity = 131072;
    bool transaction_started = false;
    u32 scoped_role_id = 0;
    mysql_error[0] = 0;

    if (db == NULL || !db_valid ||
        !vm_net_mock_mysql_account_hex_for(account_id, account_hex))
        return false;
    if (!vm_net_mock_role_ensure_pk_columns())
        return false;
    if (!vm_net_mock_role_ensure_arena_designation_column())
        return false;
    if (!vm_net_mock_role_ensure_pixi_cosmetics_columns())
        return false;
    if (timed_effect != NULL &&
        (!vm_net_mock_role_item_effect_is_valid(timed_effect) ||
         !vm_net_mock_role_prepare_item_effect_schema()))
    {
        return false;
    }
    memcpy(db->magic, "JHR1", 4);
    db->version = VM_NET_MOCK_ROLE_DB_VERSION;
    if (db->roleCount > VM_NET_MOCK_ROLE_DB_MAX_ROLES)
        db->roleCount = VM_NET_MOCK_ROLE_DB_MAX_ROLES;
    if (db->roleCount == 0)
        db->activeRoleId = 0;
    if (!full_snapshot)
    {
        scoped_role_id = scope_role_id != 0 ? scope_role_id : db->activeRoleId;
        if (db->roleCount == 0 ||
            scoped_role_id == 0)
        {
            return false;
        }
        bool found = false;
        for (u32 i = 0; i < db->roleCount; ++i)
        {
            if (db->roles[i].roleId == scoped_role_id)
            {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }

    bulk_query = (char *)malloc(bulk_capacity);
    if (bulk_query == NULL)
        return false;
    if (!vm_mysql_exec("START TRANSACTION"))
    {
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        goto failed;
    }
    transaction_started = true;
    snprintf(query, sizeof(query),
             "INSERT INTO account_role_state(account_id,format_version,active_role_id,role_count) "
             "VALUES(CAST(X'%s' AS CHAR),%u,%u,%u) ON DUPLICATE KEY UPDATE "
             "format_version=VALUES(format_version),active_role_id=VALUES(active_role_id),role_count=VALUES(role_count)",
             account_hex, VM_NET_MOCK_ROLE_DB_VERSION,
             db->activeRoleId, db->roleCount);
    if (!vm_mysql_exec(query))
    {
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        goto failed;
    }
    /* Keep existing role rows alive while reconciling a full role snapshot. Guilds,
     * guild_members and guild_applications intentionally reference these rows
     * with ON DELETE CASCADE, so the former delete-and-reinsert save erased a
     * role's guild every time role-select/position/battle state was persisted.
     * Delete only roles that really disappeared from the account, move the
     * surviving role_index values out of the live range to avoid unique-index
     * collisions, then upsert them in their current order. */
    if (full_snapshot && db->roleCount == 0)
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM account_roles WHERE account_id=CAST(X'%s' AS CHAR)",
                 account_hex);
    }
    else if (full_snapshot)
    {
        size_t query_len = (size_t)snprintf(
            query, sizeof(query),
            "DELETE FROM account_roles WHERE account_id=CAST(X'%s' AS CHAR) AND role_id NOT IN (",
            account_hex);
        for (u32 i = 0; i < db->roleCount; ++i)
        {
            int written = snprintf(query + query_len, sizeof(query) - query_len,
                                   "%s%u", i ? "," : "",
                                   db->roles[i].roleId);
            if (written < 0 || (size_t)written >= sizeof(query) - query_len)
            {
                snprintf(mysql_error, sizeof(mysql_error), "role-id delete query too large");
                goto failed;
            }
            query_len += (size_t)written;
        }
        if (query_len + 2 > sizeof(query))
        {
            snprintf(mysql_error, sizeof(mysql_error), "role-id delete query too large");
            goto failed;
        }
        query[query_len++] = ')';
        query[query_len] = 0;
    }
    if (full_snapshot && !vm_mysql_exec(query))
    {
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        goto failed;
    }
    if (full_snapshot && db->roleCount != 0)
    {
        snprintf(query, sizeof(query),
                 "UPDATE account_roles SET role_index=role_index+128 "
                 "WHERE account_id=CAST(X'%s' AS CHAR)", account_hex);
        if (!vm_mysql_exec(query))
        {
            snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
            goto failed;
        }
    }
    if (full_snapshot)
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM account_role_equipment WHERE account_id=CAST(X'%s' AS CHAR)",
                 account_hex);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM account_role_equipment WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
                 account_hex, scoped_role_id);
    }
    if (!vm_mysql_exec(query))
    {
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        goto failed;
    }
    if (full_snapshot)
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM account_role_backpack WHERE account_id=CAST(X'%s' AS CHAR)",
                 account_hex);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM account_role_backpack WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
                 account_hex, scoped_role_id);
    }
    if (!vm_mysql_exec(query))
    {
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        goto failed;
    }

    for (u32 i = 0; i < db->roleCount; ++i)
    {
        const vm_net_mock_role_state *role = &db->roles[i];
        if (!full_snapshot && role->roleId != scoped_role_id)
            continue;
        char name_hex[sizeof(role->name) * 2 + 1];
        char scene_hex[sizeof(role->scene) * 2 + 1];
        size_t name_len = vm_mock_mysql_bounded_strlen(role->name, sizeof(role->name));
        size_t scene_len = vm_mock_mysql_bounded_strlen(role->scene, sizeof(role->scene));
        if (name_len >= sizeof(role->name) || scene_len >= sizeof(role->scene) ||
            (name_len && vm_mysql_hex_encode(role->name, name_len, name_hex, sizeof(name_hex)) == 0) ||
            (scene_len && vm_mysql_hex_encode(role->scene, scene_len, scene_hex, sizeof(scene_hex)) == 0))
        {
            snprintf(mysql_error, sizeof(mysql_error), "invalid role text at index %u", i);
            goto failed;
        }
        if (!name_len)
            name_hex[0] = 0;
        if (!scene_len)
            scene_hex[0] = 0;
        snprintf(query, sizeof(query),
                 "INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,pk_points,pk_pve_streak,scene,pos_x,pos_y,backpack_item_count,designation_id,arena_designation_mask,special_designation_mask,next_backpack_seq,unlocked_chenghao_ids,unlocked_wing_ids,unlocked_mount_ids,equipped_chenghao_id,equipped_wing_id,equipped_mount_id) "
                 "VALUES(CAST(X'%s' AS CHAR),%u,%u,X'%s',%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,X'%s',%u,%u,%u,%u,%u,%u,%u,'%s','%s','%s',%u,%u,%u) "
                 "ON DUPLICATE KEY UPDATE role_index=VALUES(role_index),role_name=VALUES(role_name),"
                 "job=VALUES(job),sex=VALUES(sex),backpack_capacity=VALUES(backpack_capacity),"
                 "level=VALUES(level),exp=VALUES(exp),hp=VALUES(hp),hp_max=VALUES(hp_max),"
                 "mp=VALUES(mp),mp_max=VALUES(mp_max),money=VALUES(money),wcoin=VALUES(wcoin),"
                 "pk_points=VALUES(pk_points),pk_pve_streak=VALUES(pk_pve_streak),"
                 "scene=VALUES(scene),pos_x=VALUES(pos_x),pos_y=VALUES(pos_y),"
                 "backpack_item_count=VALUES(backpack_item_count),designation_id=VALUES(designation_id),"
                 "arena_designation_mask=VALUES(arena_designation_mask),"
                 "special_designation_mask=VALUES(special_designation_mask),"
                 "next_backpack_seq=VALUES(next_backpack_seq),"
                 "unlocked_chenghao_ids=VALUES(unlocked_chenghao_ids),"
                 "unlocked_wing_ids=VALUES(unlocked_wing_ids),"
                 "unlocked_mount_ids=VALUES(unlocked_mount_ids),"
                 "equipped_chenghao_id=VALUES(equipped_chenghao_id),"
                 "equipped_wing_id=VALUES(equipped_wing_id),"
                 "equipped_mount_id=VALUES(equipped_mount_id)",
                 account_hex, role->roleId, i, name_hex, role->job, role->sex,
                 role->backpackCapacity, role->level, role->exp, role->hp, role->hpMax,
                 role->mp, role->mpMax, role->money, role->wcoin, role->pkPoints,
                 (u32)role->pkPveDecayStreak, scene_hex,
                 role->x, role->y, role->backpackItemCount, role->designationId,
                 role->arenaDesignationMask, role->specialDesignationMask,
                 role->nextBackpackSeq,
                 role->unlockedChenghaoIds[0] ? role->unlockedChenghaoIds : "",
                 role->unlockedWingIds[0] ? role->unlockedWingIds : "",
                 role->unlockedMountIds[0] ? role->unlockedMountIds : "",
                 role->equippedChenghaoId, role->equippedWingId,
                 role->equippedMountId);
        if (!vm_mysql_exec(query))
        {
            snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
            goto failed;
        }
    }

    if (timed_effect != NULL)
    {
        if (full_snapshot || scoped_role_id == 0)
        {
            snprintf(mysql_error, sizeof(mysql_error), "timed effect requires active role scope");
            goto failed;
        }
        snprintf(query, sizeof(query),
                 "INSERT INTO account_role_item_effects(account_id,role_id,effect_kind,item_id,multiplier,expires_unix,paused_remaining_sec) "
                 "VALUES(CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u,0) "
                 "ON DUPLICATE KEY UPDATE item_id=VALUES(item_id),multiplier=VALUES(multiplier),"
                 "expires_unix=VALUES(expires_unix),paused_remaining_sec=0",
                 account_hex, scoped_role_id, timed_effect->kind,
                 timed_effect->itemId, timed_effect->multiplier,
                 timed_effect->expiresUnix);
        if (!vm_mysql_exec(query))
        {
            snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
            goto failed;
        }
    }

    if (!vm_net_mock_role_equipment_ensure_enhance_column())
    {
        snprintf(mysql_error, sizeof(mysql_error),
                 "equipment enhance_level column missing");
        goto failed;
    }

    size_t bulk_len = (size_t)snprintf(
        bulk_query, bulk_capacity,
        "INSERT INTO account_role_equipment(account_id,role_id,slot_index,item_id,enhance_level,item_instance_id,enhance_wash) VALUES");
    u32 bulk_rows = 0;
    for (u32 i = 0; i < db->roleCount; ++i)
    {
        const vm_net_mock_role_state *role = &db->roles[i];
        if (!full_snapshot && role->roleId != scoped_role_id)
            continue;
        for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
        {
            char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];
            u16 enhance = role->equippedEnhanceLevels[slot];
            if (role->equippedItemIds[slot] == 0)
                continue;
            if (enhance > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
                enhance = VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL;
            (void)vm_net_mock_enhance_wash_sql_text(&role->equippedEnhanceWash[slot],
                                                    washText, sizeof(washText));
            int written = snprintf(bulk_query + bulk_len, bulk_capacity - bulk_len,
                                   "%s(CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u,'%s')",
                                   bulk_rows ? "," : "", account_hex, role->roleId,
                                   slot, role->equippedItemIds[slot], enhance,
                                   role->equippedEnhanceInstanceIds[slot], washText);
            if (written < 0 || (size_t)written >= bulk_capacity - bulk_len)
            {
                snprintf(mysql_error, sizeof(mysql_error), "equipment query too large");
                goto failed;
            }
            bulk_len += (size_t)written;
            ++bulk_rows;
        }
    }
    if (bulk_rows && !vm_mysql_exec(bulk_query))
    {
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        goto failed;
    }

    bulk_len = (size_t)snprintf(
        bulk_query, bulk_capacity,
        "INSERT INTO account_role_backpack(account_id,role_id,slot_index,item_id,item_seq,item_count,enhance_level,item_instance_id,enhance_wash) VALUES");
    bulk_rows = 0;
    for (u32 i = 0; i < db->roleCount; ++i)
    {
        const vm_net_mock_role_state *role = &db->roles[i];
        if (!full_snapshot && role->roleId != scoped_role_id)
            continue;
        for (u32 slot = 0; slot < VM_NET_MOCK_BACKPACK_MAX_ITEMS; ++slot)
        {
            const vm_net_mock_backpack_item_state *item = &role->backpackItems[slot];
            char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];
            /* Zero-count rows are removed from the live bag; never re-insert. */
            if (item->itemId == 0 || item->count == 0)
                continue;
            (void)vm_net_mock_enhance_wash_sql_text(&item->enhanceWash, washText,
                                                    sizeof(washText));
            int written = snprintf(bulk_query + bulk_len, bulk_capacity - bulk_len,
                                   "%s(CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u,%u,%u,'%s')",
                                   bulk_rows ? "," : "", account_hex, role->roleId,
                                   slot, item->itemId, item->seq, item->count,
                                   item->enhanceLevel, item->enhanceInstanceId,
                                   washText);
            if (written < 0 || (size_t)written >= bulk_capacity - bulk_len)
            {
                snprintf(mysql_error, sizeof(mysql_error), "backpack query too large");
                goto failed;
            }
            bulk_len += (size_t)written;
            ++bulk_rows;
        }
    }
    if (bulk_rows && !vm_mysql_exec(bulk_query))
    {
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        goto failed;
    }

    for (u32 i = 0; i < mapping_count; ++i)
    {
        if (old_ids == NULL || new_ids == NULL || old_ids[i] == new_ids[i])
            continue;
        snprintf(query, sizeof(query),
                 "UPDATE friendships SET owner_role_id=%u WHERE owner_account_id=CAST(X'%s' AS CHAR) AND owner_role_id=%u",
                 new_ids[i], account_hex, old_ids[i]);
        if (!vm_mysql_exec(query))
        {
            snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
            goto failed;
        }
        snprintf(query, sizeof(query),
                 "UPDATE friendships SET target_role_id=%u WHERE target_account_id=CAST(X'%s' AS CHAR) AND target_role_id=%u",
                 new_ids[i], account_hex, old_ids[i]);
        if (!vm_mysql_exec(query))
        {
            snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
            goto failed;
        }
    }

    /*
     * Warehouse rows must commit with the backpack snapshot.  Deposit used to
     * auto-commit warehouse first; a later backpack save failure left the item
     * in both account_role_warehouse and account_role_backpack.
     */
    if (warehouse != NULL && warehouse->loaded)
    {
        const char *warehouseAccount = warehouse->accountId;
        if (warehouseAccount[0] != 0 && account_id != NULL &&
            strcmp(warehouseAccount, account_id) == 0)
        {
            for (u32 i = 0; i < db->roleCount; ++i)
            {
                const vm_net_mock_role_state *role =
                    &db->roles[i];
                if (!full_snapshot && role->roleId != scoped_role_id)
                    continue;
                if (role->roleId != warehouse->roleId)
                    continue;
                if (!vm_net_mock_warehouse_write_sql(role, account_hex, warehouse,
                                                     mysql_error,
                                                     sizeof(mysql_error)))
                {
                    if (mysql_error[0] == 0)
                        snprintf(mysql_error, sizeof(mysql_error), "%s",
                                 vm_mysql_last_error());
                    goto failed;
                }
                break;
            }
        }
    }

    if (!vm_mysql_exec("COMMIT"))
    {
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        goto failed;
    }
    free(bulk_query);
    if (clear_process_dirty_flags)
    {
        g_vm_net_mock_role_position_dirty = false;
        g_vm_net_mock_role_inventory_dirty = false;
        /*
         * Sync writers (teleport / warehouse / disconnect flush) commit a newer
         * authority than any snapshot already queued for async flush.  Bump
         * persistGeneration and refresh account_state.roleDb so sticky capture
         * cannot restore pre-write coordinates or backpack rows, and so the
         * in-flight deferred job is superseded instead of overwriting MySQL.
         */
        vm_mock_service_account_note_durable_role_write(
            account_id,
            db,
            true,
            warehouse,
            true,
            true,
            reason ? reason : "role-db-save");
    }
    vm_autotest_note("mock_role_db_mysql_save account=%s reason=%s roles=%u active=%u scope=%s storage=relational\n",
                     account_id ? account_id : "-", reason ? reason : "state",
                     db->roleCount, db->activeRoleId,
                     full_snapshot ? "full" : "active");
    return true;

failed:
    if (transaction_started)
        vm_mysql_exec("ROLLBACK");
    vm_autotest_note("mock_role_db_mysql_save_failed account=%s reason=%s error=%s\n",
                     account_id ? account_id : "-", reason ? reason : "state",
                     mysql_error[0] ? mysql_error : "unknown");
    free(bulk_query);
    return false;
}

static bool vm_net_mock_role_db_save_relational(const char *reason,
                                                 const u32 *old_ids,
                                                 const u32 *new_ids,
                                                 u32 mapping_count,
                                                 bool full_snapshot,
                                                 const vm_net_mock_role_item_effect *timed_effect)
{
    return vm_net_mock_role_db_save_relational_ex(
        reason,
        g_vm_mock_service_active_account_id,
        &g_vm_net_mock_role_db,
        g_vm_net_mock_role_db_valid,
        &g_vm_net_mock_warehouse,
        true,
        old_ids,
        new_ids,
        mapping_count,
        full_snapshot,
        0,
        timed_effect);
}

static bool vm_net_mock_role_db_save(const char *reason)
{
    return vm_net_mock_role_db_save_relational(reason, NULL, NULL, 0, false, NULL);
}

/* Persist one role row by role_id (admin money/wcoin may target non-active). */
static bool vm_net_mock_role_db_save_role(const char *reason, u32 roleId)
{
    if (roleId == 0)
        return vm_net_mock_role_db_save(reason);
    return vm_net_mock_role_db_save_relational_ex(
        reason,
        g_vm_mock_service_active_account_id,
        &g_vm_net_mock_role_db,
        g_vm_net_mock_role_db_valid,
        &g_vm_net_mock_warehouse,
        true,
        NULL,
        NULL,
        0,
        false,
        roleId,
        NULL);
}

static void vm_net_mock_role_mark_inventory_dirty(const char *reason)
{
    bool wasDirty = g_vm_net_mock_role_inventory_dirty;
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    g_vm_net_mock_role_inventory_dirty = true;
    if (wasDirty)
        return;
    printf("[info][network] mock_role_persist_deferred reason=%s account=%s role=%u "
           "evidence=flush-after-cbmr-or-disconnect\n",
           reason ? reason : "state",
           g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
           role ? role->roleId : 0);
}

static u32 vm_net_mock_role_active_exp_card_multiplier(
    const vm_net_mock_role_state *role)
{
    vm_net_mock_role_item_effect effect;

    memset(&effect, 0, sizeof(effect));
    if (!vm_net_mock_role_get_active_timed_item_effect(
            role, VM_NET_MOCK_ROLE_ITEM_EFFECT_EXP_CARD, &effect))
    {
        printf("[error][mock-service] exp_card_state_read_failed account=%s role=%u error=%s\n",
               g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
               role ? role->roleId : 0, vm_mysql_last_error());
        return 1;
    }
    return effect.expiresUnix != 0 ? effect.multiplier : 1;
}

/* Wall-clock seconds until expires_unix; 0 when inactive or already due.
 * Paused offline cards expose a synthetic expires via get_active, so this
 * still reports the frozen remainder. */
static u32 vm_net_mock_role_active_exp_card_remaining_seconds(
    const vm_net_mock_role_state *role)
{
    vm_net_mock_role_item_effect effect;
    u32 now = (u32)time(NULL);

    memset(&effect, 0, sizeof(effect));
    if (!vm_net_mock_role_get_active_timed_item_effect(
            role, VM_NET_MOCK_ROLE_ITEM_EFFECT_EXP_CARD, &effect) ||
        effect.expiresUnix == 0 || effect.expiresUnix <= now)
    {
        return 0;
    }
    return effect.expiresUnix - now;
}

static const char *vm_net_mock_role_pausable_timed_effect_name(u8 effectKind)
{
    if (effectKind == VM_NET_MOCK_ROLE_ITEM_EFFECT_EXP_CARD)
        return "exp-card";
    if (effectKind == VM_NET_MOCK_ROLE_ITEM_EFFECT_BATTLE_INSIGHT)
        return "battle-insight";
    return "timed-effect";
}

static bool vm_net_mock_role_timed_effect_is_pausable(u8 effectKind)
{
    return effectKind == VM_NET_MOCK_ROLE_ITEM_EFFECT_EXP_CARD ||
           effectKind == VM_NET_MOCK_ROLE_ITEM_EFFECT_BATTLE_INSIGHT;
}

/*
 * Freeze pausable timed-effect duration across disconnect: store leftover
 * seconds in paused_remaining_sec and clear wall-clock expires_unix.
 * Idempotent if already paused.  Combat pills keep wall-clock expiry.
 */
static bool vm_net_mock_role_pausable_timed_effect_pause_on_logout(
    const char *accountId, u32 roleId, u8 effectKind)
{
    char account_hex[129];
    char query[768];
    vm_mock_mysql_role_item_effect_context context;
    u32 now = (u32)time(NULL);
    u32 remaining = 0;
    const char *kindName = vm_net_mock_role_pausable_timed_effect_name(effectKind);

    if (roleId == 0 || accountId == NULL || accountId[0] == '\0' ||
        !vm_net_mock_role_timed_effect_is_pausable(effectKind) ||
        !vm_net_mock_mysql_account_hex_for(accountId, account_hex) ||
        !vm_net_mock_role_prepare_item_effect_schema())
    {
        return false;
    }

    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT item_id,multiplier,expires_unix,paused_remaining_sec "
             "FROM account_role_item_effects "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND effect_kind=%u",
             account_hex, roleId, (u32)effectKind);
    if (!vm_mysql_query(query, vm_mock_mysql_role_item_effect_row, &context) ||
        context.invalid)
    {
        return false;
    }
    if (!context.found)
        return true;
    if (context.pausedRemainingSec != 0)
    {
        printf("[info][mock-service] %s_pause_skip account=%s role=%u "
               "reason=already-paused remaining=%u mult=%u\n",
               kindName, accountId, roleId, context.pausedRemainingSec,
               context.effect.multiplier);
        return true;
    }
    if (context.effect.expiresUnix <= now)
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM account_role_item_effects WHERE "
                 "account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND effect_kind=%u",
                 account_hex, roleId, (u32)effectKind);
        if (!vm_mysql_exec(query))
            return false;
        printf("[info][mock-service] %s_pause_expired_clear account=%s role=%u\n",
               kindName, accountId, roleId);
        return true;
    }

    remaining = context.effect.expiresUnix - now;
    snprintf(query, sizeof(query),
             "UPDATE account_role_item_effects SET expires_unix=0,paused_remaining_sec=%u "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND effect_kind=%u",
             remaining, account_hex, roleId, (u32)effectKind);
    if (!vm_mysql_exec(query))
        return false;
    printf("[info][mock-service] %s_paused account=%s role=%u remaining_s=%u "
           "mult=%u item=%u evidence=offline-does-not-consume-duration\n",
           kindName, accountId, roleId, remaining, context.effect.multiplier,
           context.effect.itemId);
    return true;
}

static bool vm_net_mock_role_pausable_timed_effect_resume_on_login(
    vm_net_mock_role_state *role, u8 effectKind)
{
    char account_hex[129];
    char query[768];
    vm_mock_mysql_role_item_effect_context context;
    u32 now = (u32)time(NULL);
    u32 expires = 0;
    const char *kindName = vm_net_mock_role_pausable_timed_effect_name(effectKind);

    if (role == NULL || role->roleId == 0 ||
        !vm_net_mock_role_timed_effect_is_pausable(effectKind) ||
        !vm_net_mock_mysql_account_hex(account_hex) ||
        !vm_net_mock_role_prepare_item_effect_schema())
    {
        return false;
    }

    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT item_id,multiplier,expires_unix,paused_remaining_sec "
             "FROM account_role_item_effects "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND effect_kind=%u",
             account_hex, role->roleId, (u32)effectKind);
    if (!vm_mysql_query(query, vm_mock_mysql_role_item_effect_row, &context) ||
        context.invalid)
    {
        return false;
    }
    if (!context.found || context.pausedRemainingSec == 0)
        return true;

    if (now > 0xffffffffu - context.pausedRemainingSec)
        expires = 0xffffffffu;
    else
        expires = now + context.pausedRemainingSec;
    snprintf(query, sizeof(query),
             "UPDATE account_role_item_effects SET expires_unix=%u,paused_remaining_sec=0 "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND effect_kind=%u",
             expires, account_hex, role->roleId, (u32)effectKind);
    if (!vm_mysql_exec(query))
        return false;
    printf("[info][mock-service] %s_resumed account=%s role=%u remaining_s=%u "
           "expires=%u mult=%u item=%u evidence=resume-on-login\n",
           kindName,
           g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
           role->roleId, context.pausedRemainingSec, expires,
           context.effect.multiplier, context.effect.itemId);
    return true;
}

static bool vm_net_mock_role_exp_card_pause_on_logout(const char *accountId,
                                                      u32 roleId)
{
    return vm_net_mock_role_pausable_timed_effect_pause_on_logout(
        accountId, roleId, VM_NET_MOCK_ROLE_ITEM_EFFECT_EXP_CARD);
}

static bool vm_net_mock_role_exp_card_resume_on_login(vm_net_mock_role_state *role)
{
    return vm_net_mock_role_pausable_timed_effect_resume_on_login(
        role, VM_NET_MOCK_ROLE_ITEM_EFFECT_EXP_CARD);
}

static bool vm_net_mock_role_battle_insight_pause_on_logout(const char *accountId,
                                                            u32 roleId)
{
    return vm_net_mock_role_pausable_timed_effect_pause_on_logout(
        accountId, roleId, VM_NET_MOCK_ROLE_ITEM_EFFECT_BATTLE_INSIGHT);
}

static bool vm_net_mock_role_battle_insight_resume_on_login(
    vm_net_mock_role_state *role)
{
    return vm_net_mock_role_pausable_timed_effect_resume_on_login(
        role, VM_NET_MOCK_ROLE_ITEM_EFFECT_BATTLE_INSIGHT);
}

/* item.dsh describes ???? as a one-hour, +20% experience status. Its
 * multiplier column therefore denotes an additive percentage for this kind,
 * unlike the factor stored for experience cards. */
static u32 vm_net_mock_role_active_battle_exp_bonus_percent(
    const vm_net_mock_role_state *role)
{
    vm_net_mock_role_item_effect effect;

    memset(&effect, 0, sizeof(effect));
    if (!vm_net_mock_role_get_active_timed_item_effect(
            role, VM_NET_MOCK_ROLE_ITEM_EFFECT_BATTLE_INSIGHT, &effect))
    {
        printf("[error][mock-service] battle_insight_state_read_failed account=%s role=%u error=%s\n",
               g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
               role ? role->roleId : 0, vm_mysql_last_error());
        return 0;
    }
    return effect.expiresUnix != 0 ? effect.multiplier : 0;
}

/* Wall-clock seconds until battle-insight expiry; 0 when inactive/due.
 * Paused offline rows expose synthetic expires via get_active. */
static u32 vm_net_mock_role_active_battle_insight_remaining_seconds(
    const vm_net_mock_role_state *role)
{
    vm_net_mock_role_item_effect effect;
    u32 now = (u32)time(NULL);

    memset(&effect, 0, sizeof(effect));
    if (!vm_net_mock_role_get_active_timed_item_effect(
            role, VM_NET_MOCK_ROLE_ITEM_EFFECT_BATTLE_INSIGHT, &effect) ||
        effect.expiresUnix == 0 || effect.expiresUnix <= now)
    {
        return 0;
    }
    return effect.expiresUnix - now;
}

static u8 vm_net_mock_role_active_exp_card_flag(void)
{
    return vm_net_mock_role_active_exp_card_multiplier(vm_net_mock_active_role()) > 1
               ? 1
               : 0;
}

/* Top-left status icon gate (JianghuOL.CBE:0x01011AF8 + expcard).  Experience
 * cards and battle insight share the same client slot: non-empty 7/31 expinfo
 * plus expcard!=0.  Combat still uses the separate card multiplier / insight
 * percent helpers. */
static u8 vm_net_mock_role_active_status_icon_flag(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (vm_net_mock_role_active_exp_card_multiplier(role) > 1)
        return 1;
    if (vm_net_mock_role_active_battle_exp_bonus_percent(role) != 0)
        return 1;
    return 0;
}

static u32 vm_net_mock_role_active_combat_pill_attack_bonus_percent(
    const vm_net_mock_role_state *role)
{
    vm_net_mock_role_item_effect effect;

    memset(&effect, 0, sizeof(effect));
    if (!vm_net_mock_role_get_active_timed_item_effect(
            role, VM_NET_MOCK_ROLE_ITEM_EFFECT_COMBAT_PILL, &effect))
    {
        return 0;
    }
    return effect.expiresUnix != 0 ? effect.multiplier : 0;
}

/* Wall-clock seconds until combat-pill expiry; 0 when inactive/due. */
static u32 vm_net_mock_role_active_combat_pill_remaining_seconds(
    const vm_net_mock_role_state *role)
{
    vm_net_mock_role_item_effect effect;
    u32 now = (u32)time(NULL);

    memset(&effect, 0, sizeof(effect));
    if (!vm_net_mock_role_get_active_timed_item_effect(
            role, VM_NET_MOCK_ROLE_ITEM_EFFECT_COMBAT_PILL, &effect) ||
        effect.expiresUnix == 0 || effect.expiresUnix <= now)
    {
        return 0;
    }
    return effect.expiresUnix - now;
}

/* Active combat-pill item id (829/830), or 0 when none. */
static u32 vm_net_mock_role_active_combat_pill_item_id(
    const vm_net_mock_role_state *role)
{
    vm_net_mock_role_item_effect effect;

    memset(&effect, 0, sizeof(effect));
    if (!vm_net_mock_role_get_active_timed_item_effect(
            role, VM_NET_MOCK_ROLE_ITEM_EFFECT_COMBAT_PILL, &effect) ||
        effect.expiresUnix == 0)
    {
        return 0;
    }
    return effect.itemId;
}

/* Exp-card stack ceiling: same multiplier may extend remaining time, but never
 * past 8 hours wall-clock leftover (offline pause does not count). */
#define VM_NET_MOCK_EXP_CARD_MAX_REMAINING_SEC (8u * 3600u)

/* GBK tips for exp-card stack rejects (iteminfo / 7/1 hint). */
static const char k_vm_net_mock_exp_card_tip_same_effect[] =
    "\xCD\xAC\xC0\xE0\xD0\xA7\xB9\xFB\xD2\xD1\xC9\xFA\xD0\xA7\xA3\xAC\xC7\xEB\xB5\xC8\xB4\xFD\xBD\xE1\xCA\xF8\xBA\xF3\xD4\xD9\xCA\xB9\xD3\xC3\xA1\xA3";
static const char k_vm_net_mock_exp_card_tip_max_duration[] =
    "\xBE\xAD\xD1\xE9\xBF\xA8\xB3\xD6\xD0\xF8\xCA\xB1\xBC\xE4\xD2\xD1\xB4\xEF\xC9\xCF\xCF\xDE\xA3\xA8\x38\xD0\xA1\xCA\xB1\xA3\xA9\xA3\xAC\xC7\xEB\xB5\xC8\xB4\xFD\xCA\xB1\xBC\xE4\xBC\xF5\xC9\xD9\xBA\xF3\xD4\xD9\xCA\xB9\xD3\xC3\xA1\xA3";

/* The backpack decrement and the timed effect belong to one durable action.
 * The row is only changed in memory before the relational transaction has
 * committed; on any failure restore the exact previous role state so a retry
 * cannot lose an item or create an unbacked effect.
 *
 * Experience cards may stack only at the same multiplier: remaining wall-clock
 * end gains another item duration, item_id/multiplier stay on the active card,
 * and leftover must stay <= 8h.  Different multipliers reject without consume.
 * Battle insight may still stack duration.  Combat pills reject a second use
 * until expiry.  Effects live in account_role_item_effects and are not cleared
 * on disconnect � pausable kinds freeze duration offline and resume on login. */
static bool vm_net_mock_role_consume_backpack_item_with_timed_effect(
    vm_net_mock_role_state *role, u32 itemId, u16 seq,
    const vm_net_mock_role_item_effect *effect, u32 *remainingOut,
    const char *reason, const char **failInfoOut)
{
    vm_net_mock_role_item_effect active;
    vm_net_mock_role_item_effect stacked;
    vm_net_mock_role_state before;
    u32 remaining = 0;
    u32 now = (u32)time(NULL);
    u32 addSeconds = 0;
    u32 baseExpires = 0;
    u32 remainingBefore = 0;
    u32 remainingAfter = 0;
    bool stackable = false;

    if (remainingOut)
        *remainingOut = 0;
    if (failInfoOut)
        *failInfoOut = NULL;
    if (role == NULL || effect == NULL || effect->itemId != itemId ||
        !vm_net_mock_role_item_effect_is_valid(effect))
    {
        return false;
    }
    stacked = *effect;
    stackable = effect->kind == VM_NET_MOCK_ROLE_ITEM_EFFECT_EXP_CARD ||
                effect->kind == VM_NET_MOCK_ROLE_ITEM_EFFECT_BATTLE_INSIGHT;
    memset(&active, 0, sizeof(active));
    if (!vm_net_mock_role_get_active_timed_item_effect(role, effect->kind, &active))
        return false;
    if (active.expiresUnix != 0)
    {
        if (!stackable)
        {
            printf("[info][network] mock_special_item_rejected_active account=%s role=%u kind=%u active_item=%u active_until=%u requested_item=%u\n",
                   g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
                   role->roleId, effect->kind, active.itemId, active.expiresUnix,
                   itemId);
            if (failInfoOut)
                *failInfoOut = k_vm_net_mock_exp_card_tip_same_effect;
            return false;
        }
        if (effect->kind == VM_NET_MOCK_ROLE_ITEM_EFFECT_EXP_CARD &&
            active.multiplier != effect->multiplier)
        {
            printf("[info][network] mock_exp_card_rejected_different_type account=%s role=%u "
                   "active_item=%u active_mult=%u active_until=%u requested_item=%u "
                   "requested_mult=%u\n",
                   g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
                   role->roleId, active.itemId, active.multiplier, active.expiresUnix,
                   itemId, effect->multiplier);
            if (failInfoOut)
                *failInfoOut = k_vm_net_mock_exp_card_tip_same_effect;
            return false;
        }
        if (effect->expiresUnix <= now)
            return false;
        addSeconds = effect->expiresUnix - now;
        baseExpires = active.expiresUnix > now ? active.expiresUnix : now;
        remainingBefore = active.expiresUnix > now ? active.expiresUnix - now : 0;
        if (addSeconds > 0xffffffffu - baseExpires)
            stacked.expiresUnix = 0xffffffffu;
        else
            stacked.expiresUnix = baseExpires + addSeconds;
        /* Keep the active card identity; only extend its end time. */
        stacked.multiplier = active.multiplier;
        stacked.itemId = active.itemId;
        remainingAfter =
            stacked.expiresUnix > now ? stacked.expiresUnix - now : 0;
        if (effect->kind == VM_NET_MOCK_ROLE_ITEM_EFFECT_EXP_CARD &&
            remainingAfter > VM_NET_MOCK_EXP_CARD_MAX_REMAINING_SEC)
        {
            printf("[info][network] mock_exp_card_rejected_max_duration account=%s role=%u "
                   "active_item=%u active_mult=%u remaining_before=%u add_s=%u "
                   "remaining_after=%u max_s=%u\n",
                   g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
                   role->roleId, active.itemId, active.multiplier, remainingBefore,
                   addSeconds, remainingAfter, VM_NET_MOCK_EXP_CARD_MAX_REMAINING_SEC);
            if (failInfoOut)
                *failInfoOut = k_vm_net_mock_exp_card_tip_max_duration;
            return false;
        }
        if (!vm_net_mock_role_item_effect_is_valid(&stacked))
            return false;
        printf("[info][network] mock_%s_stacked account=%s role=%u active_item=%u active_until=%u add_s=%u new_item=%u new_mult=%u new_until=%u\n",
               effect->kind == VM_NET_MOCK_ROLE_ITEM_EFFECT_EXP_CARD ?
                   "exp_card" : "battle_insight",
               g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
               role->roleId, active.itemId, active.expiresUnix, addSeconds,
               stacked.itemId, stacked.multiplier, stacked.expiresUnix);
    }

    before = *role;
    if (!vm_net_mock_role_consume_backpack_item(role, itemId, seq, 1, &remaining))
        return false;
    if (!vm_net_mock_role_db_save_relational(
            reason ? reason : "special-item-use", NULL, NULL, 0, false, &stacked))
    {
        *role = before;
        printf("[error][mock-service] special_item_persist_failed account=%s role=%u item=%u seq=%u kind=%u error=%s\n",
               g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
               before.roleId, itemId, seq, effect->kind, vm_mysql_last_error());
        return false;
    }
    if (remainingOut)
        *remainingOut = remaining;
    return true;
}

static bool g_vm_net_mock_offline_practise_schema_prepared = false;
static u8 g_vm_net_mock_offline_practise_login_flag = 0;
static char g_vm_net_mock_offline_practise_login_info[160];

static u32 vm_net_mock_practise_today_ymd(void)
{
    time_t now = time(NULL);
    struct tm localTm;
#if defined(_WIN32)
    if (localtime_s(&localTm, &now) != 0)
        return 0;
#else
    if (localtime_r(&now, &localTm) == NULL)
        return 0;
#endif
    return ((u32)(localTm.tm_year + 1900) * 10000u) +
           ((u32)(localTm.tm_mon + 1) * 100u) + (u32)localTm.tm_mday;
}

static bool vm_net_mock_role_prepare_offline_practise_schema(void)
{
    if (g_vm_net_mock_offline_practise_schema_prepared)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_role_offline_practise ("
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "bank_minutes INT UNSIGNED NOT NULL DEFAULT 0,"
            "last_logout_unix INT UNSIGNED NOT NULL DEFAULT 0,"
            "today_ymd INT UNSIGNED NOT NULL DEFAULT 0,"
            "today_used_minutes INT UNSIGNED NOT NULL DEFAULT 0,"
            "last_settle_exp INT UNSIGNED NOT NULL DEFAULT 0,"
            "last_settle_minutes INT UNSIGNED NOT NULL DEFAULT 0,"
            "is_gold TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(account_id,role_id),"
            "CONSTRAINT fk_account_role_offline_practise_role "
            "FOREIGN KEY(account_id,role_id) "
            "REFERENCES account_roles(account_id,role_id) ON DELETE CASCADE"
            ") ENGINE=InnoDB"))
    {
        printf("[error][mock-service] offline_practise_schema_prepare error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_offline_practise_schema_prepared = true;
    return true;
}

typedef struct
{
    vm_net_mock_offline_practise_state state;
    bool found;
    bool invalid;
} vm_mock_mysql_offline_practise_context;

static bool vm_mock_mysql_offline_practise_row(void *context_value,
                                               unsigned int column_count,
                                               const char *const *values,
                                               const size_t *lengths)
{
    vm_mock_mysql_offline_practise_context *context =
        (vm_mock_mysql_offline_practise_context *)context_value;
    u32 number[7];

    if (context == NULL || context->found || column_count != 7)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    memset(number, 0, sizeof(number));
    for (u32 i = 0; i < 7; ++i)
    {
        if (!vm_mock_mysql_parse_u32(values[i], lengths[i], &number[i]))
        {
            context->invalid = true;
            return true;
        }
    }
    if (number[6] > 1u)
    {
        context->invalid = true;
        return true;
    }
    context->state.bankMinutes = number[0];
    context->state.lastLogoutUnix = number[1];
    context->state.todayYmd = number[2];
    context->state.todayUsedMinutes = number[3];
    context->state.lastSettleExp = number[4];
    context->state.lastSettleMinutes = number[5];
    context->state.isGold = (u8)number[6];
    context->found = true;
    return true;
}

static bool vm_net_mock_role_offline_practise_load_for(
    const char *accountId, u32 roleId, vm_net_mock_offline_practise_state *out)
{
    char account_hex[129];
    char query[640];
    vm_mock_mysql_offline_practise_context context;

    if (out)
        memset(out, 0, sizeof(*out));
    if (roleId == 0 || !vm_net_mock_mysql_account_hex_for(accountId, account_hex) ||
        !vm_net_mock_role_prepare_offline_practise_schema())
    {
        return false;
    }
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT bank_minutes,last_logout_unix,today_ymd,today_used_minutes,"
             "last_settle_exp,last_settle_minutes,is_gold "
             "FROM account_role_offline_practise WHERE account_id=CAST(X'%s' AS CHAR) "
             "AND role_id=%u",
             account_hex, roleId);
    if (!vm_mysql_query(query, vm_mock_mysql_offline_practise_row, &context) ||
        context.invalid)
    {
        return false;
    }
    if (out && context.found)
        *out = context.state;
    return true;
}

static bool vm_net_mock_role_offline_practise_save_for(
    const char *accountId, u32 roleId,
    const vm_net_mock_offline_practise_state *state)
{
    char account_hex[129];
    char query[768];

    if (state == NULL || roleId == 0 ||
        !vm_net_mock_mysql_account_hex_for(accountId, account_hex) ||
        !vm_net_mock_role_prepare_offline_practise_schema())
    {
        return false;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO account_role_offline_practise("
             "account_id,role_id,bank_minutes,last_logout_unix,today_ymd,"
             "today_used_minutes,last_settle_exp,last_settle_minutes,is_gold) "
             "VALUES(CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u,%u,%u,%u) "
             "ON DUPLICATE KEY UPDATE bank_minutes=VALUES(bank_minutes),"
             "last_logout_unix=VALUES(last_logout_unix),today_ymd=VALUES(today_ymd),"
             "today_used_minutes=VALUES(today_used_minutes),"
             "last_settle_exp=VALUES(last_settle_exp),"
             "last_settle_minutes=VALUES(last_settle_minutes),"
             "is_gold=VALUES(is_gold)",
             account_hex, roleId, state->bankMinutes, state->lastLogoutUnix,
             state->todayYmd, state->todayUsedMinutes, state->lastSettleExp,
             state->lastSettleMinutes, state->isGold);
    return vm_mysql_exec(query);
}

static void vm_net_mock_offline_practise_normalize_day(
    vm_net_mock_offline_practise_state *state)
{
    u32 today = vm_net_mock_practise_today_ymd();

    if (state == NULL)
        return;
    if (state->todayYmd != today)
    {
        state->todayYmd = today;
        state->todayUsedMinutes = 0;
    }
    if (state->bankMinutes > VM_NET_MOCK_PRACTISE_BANK_MAX_MINUTES)
        state->bankMinutes = VM_NET_MOCK_PRACTISE_BANK_MAX_MINUTES;
    if (state->todayUsedMinutes > VM_NET_MOCK_PRACTISE_DAILY_MAX_MINUTES)
        state->todayUsedMinutes = VM_NET_MOCK_PRACTISE_DAILY_MAX_MINUTES;
    if (state->isGold > 1)
        state->isGold = 1;
}

static bool vm_net_mock_role_use_practise_pill(vm_net_mock_role_state *role,
                                               u16 seq, u32 *remainingOut)
{
    const char *accountId = g_vm_mock_service_active_account_id;
    vm_net_mock_role_state before;
    vm_net_mock_offline_practise_state practise;
    u32 remaining = 0;
    u32 room = 0;

    if (remainingOut)
        *remainingOut = 0;
    if (role == NULL || role->roleId == 0 || accountId == NULL)
        return false;
    memset(&practise, 0, sizeof(practise));
    if (!vm_net_mock_role_offline_practise_load_for(accountId, role->roleId,
                                                    &practise))
    {
        return false;
    }
    vm_net_mock_offline_practise_normalize_day(&practise);
    if (practise.bankMinutes >= VM_NET_MOCK_PRACTISE_BANK_MAX_MINUTES)
        return false;
    room = VM_NET_MOCK_PRACTISE_BANK_MAX_MINUTES - practise.bankMinutes;
    before = *role;
    if (!vm_net_mock_role_consume_backpack_item(
            role, VM_NET_MOCK_PRACTISE_PILL_ITEM_ID, seq, 1, &remaining))
    {
        return false;
    }
    if (!vm_net_mock_role_db_save("practise-pill-use"))
    {
        *role = before;
        return false;
    }
    if (room >= VM_NET_MOCK_PRACTISE_PILL_ADD_MINUTES)
        practise.bankMinutes += VM_NET_MOCK_PRACTISE_PILL_ADD_MINUTES;
    else
        practise.bankMinutes = VM_NET_MOCK_PRACTISE_BANK_MAX_MINUTES;
    if (!vm_net_mock_role_offline_practise_save_for(accountId, role->roleId,
                                                    &practise))
    {
        *role = before;
        (void)vm_net_mock_role_db_save("practise-pill-use-rollback");
        return false;
    }
    if (remainingOut)
        *remainingOut = remaining;
    printf("[info][mock-service] practise_pill_use account=%s role=%u bank_min=%u remaining=%u\n",
           accountId, role->roleId, practise.bankMinutes, remaining);
    return true;
}

static bool vm_net_mock_role_offline_practise_mark_logout(const char *accountId,
                                                          u32 roleId)
{
    vm_net_mock_offline_practise_state practise;
    u32 now = (u32)time(NULL);

    if (roleId == 0 || accountId == NULL || accountId[0] == '\0')
        return false;
    memset(&practise, 0, sizeof(practise));
    if (!vm_net_mock_role_offline_practise_load_for(accountId, roleId, &practise))
        return false;
    vm_net_mock_offline_practise_normalize_day(&practise);
    practise.lastLogoutUnix = now;
    if (!vm_net_mock_role_offline_practise_save_for(accountId, roleId, &practise))
        return false;
    printf("[info][mock-service] practise_logout_stamp account=%s role=%u logout=%u bank_min=%u\n",
           accountId, roleId, now, practise.bankMinutes);
    return true;
}

static bool vm_net_mock_role_offline_practise_settle_on_login(
    vm_net_mock_role_state *role, u32 *settleMinutesOut, u32 *settleExpOut)
{
    const char *accountId = g_vm_mock_service_active_account_id;
    vm_net_mock_offline_practise_state practise;
    u32 now = (u32)time(NULL);
    u32 offlineMinutes = 0;
    u32 dailyLeft = 0;
    u32 settleMinutes = 0;
    u32 level = 1;
    uint64_t exp64 = 0;
    u32 settleExp = 0;

    g_vm_net_mock_offline_practise_login_flag = 0;
    g_vm_net_mock_offline_practise_login_info[0] = 0;
    if (settleMinutesOut)
        *settleMinutesOut = 0;
    if (settleExpOut)
        *settleExpOut = 0;
    if (role == NULL || role->roleId == 0 || accountId == NULL)
        return false;
    memset(&practise, 0, sizeof(practise));
    if (!vm_net_mock_role_offline_practise_load_for(accountId, role->roleId,
                                                    &practise))
    {
        return false;
    }
    vm_net_mock_offline_practise_normalize_day(&practise);
    if (practise.lastLogoutUnix != 0 && now > practise.lastLogoutUnix)
        offlineMinutes = (now - practise.lastLogoutUnix) / 60u;
    dailyLeft = VM_NET_MOCK_PRACTISE_DAILY_MAX_MINUTES > practise.todayUsedMinutes
                    ? (VM_NET_MOCK_PRACTISE_DAILY_MAX_MINUTES -
                       practise.todayUsedMinutes)
                    : 0;
    settleMinutes = offlineMinutes;
    if (settleMinutes > practise.bankMinutes)
        settleMinutes = practise.bankMinutes;
    if (settleMinutes > dailyLeft)
        settleMinutes = dailyLeft;
    level = role->level ? role->level : 1;
    if (settleMinutes > 0)
    {
        exp64 = (uint64_t)settleMinutes * (uint64_t)level * 8ull;
        settleExp = exp64 > 0xffffffffull ? 0xffffffffu : (u32)exp64;
        practise.bankMinutes -= settleMinutes;
        practise.todayUsedMinutes += settleMinutes;
        practise.lastSettleMinutes = settleMinutes;
        practise.lastSettleExp = settleExp;
        (void)vm_net_mock_role_add_exp(role, settleExp);
        if (!vm_net_mock_role_db_save("offline-practise-settle"))
            return false;
        snprintf(g_vm_net_mock_offline_practise_login_info,
                 sizeof(g_vm_net_mock_offline_practise_login_info),
                 "\xC0\xEB\xCF\xDF\xD0\xDE\xC1\xB6%u\xB7\xD6\xD6\xD3\xA3\xAC"
                 "\xBB\xF1\xB5\xC3%u\xBE\xAD\xD1\xE9\xA1\xA3",
                 settleMinutes, settleExp);
        g_vm_net_mock_offline_practise_login_flag = 1;
    }
    practise.lastLogoutUnix = 0;
    if (!vm_net_mock_role_offline_practise_save_for(accountId, role->roleId,
                                                    &practise))
    {
        return false;
    }
    if (settleMinutesOut)
        *settleMinutesOut = settleMinutes;
    if (settleExpOut)
        *settleExpOut = settleExp;
    printf("[info][mock-service] practise_settle account=%s role=%u offline_min=%u bank_left=%u settle_min=%u level=%u exp=%u daily_used=%u\n",
           accountId, role->roleId, offlineMinutes, practise.bankMinutes,
           settleMinutes, level, settleExp, practise.todayUsedMinutes);
    return true;
}

static void vm_net_mock_role_offline_practise_panel_values(
    u32 *todayPastHourOut, u32 *todayPastMinOut, u32 *getExpOut,
    u32 *todayLastHourOut, u32 *todayLastMinOut, u32 *allLastHourOut,
    u32 *allLastMinOut, u8 *isGoldOut)
{
    const char *accountId = g_vm_mock_service_active_account_id;
    const vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_offline_practise_state practise;
    u32 todayLeft = 0;

    if (todayPastHourOut)
        *todayPastHourOut = 0;
    if (todayPastMinOut)
        *todayPastMinOut = 0;
    if (getExpOut)
        *getExpOut = 0;
    if (todayLastHourOut)
        *todayLastHourOut = 0;
    if (todayLastMinOut)
        *todayLastMinOut = 0;
    if (allLastHourOut)
        *allLastHourOut = 0;
    if (allLastMinOut)
        *allLastMinOut = 0;
    if (isGoldOut)
        *isGoldOut = 0;
    if (role == NULL || accountId == NULL)
        return;
    memset(&practise, 0, sizeof(practise));
    if (!vm_net_mock_role_offline_practise_load_for(accountId, role->roleId,
                                                    &practise))
    {
        return;
    }
    vm_net_mock_offline_practise_normalize_day(&practise);
    todayLeft = VM_NET_MOCK_PRACTISE_DAILY_MAX_MINUTES > practise.todayUsedMinutes
                    ? (VM_NET_MOCK_PRACTISE_DAILY_MAX_MINUTES -
                       practise.todayUsedMinutes)
                    : 0;
    if (todayPastHourOut)
        *todayPastHourOut = practise.todayUsedMinutes / 60u;
    if (todayPastMinOut)
        *todayPastMinOut = practise.todayUsedMinutes % 60u;
    if (getExpOut)
        *getExpOut = practise.lastSettleExp;
    if (todayLastHourOut)
        *todayLastHourOut = todayLeft / 60u;
    if (todayLastMinOut)
        *todayLastMinOut = todayLeft % 60u;
    if (allLastHourOut)
        *allLastHourOut = practise.bankMinutes / 60u;
    if (allLastMinOut)
        *allLastMinOut = practise.bankMinutes % 60u;
    if (isGoldOut)
        *isGoldOut = practise.isGold;
}

static bool vm_net_mock_role_offline_practise_set_gold(u8 isGold)
{
    const char *accountId = g_vm_mock_service_active_account_id;
    const vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_offline_practise_state practise;

    if (role == NULL || accountId == NULL)
        return false;
    memset(&practise, 0, sizeof(practise));
    if (!vm_net_mock_role_offline_practise_load_for(accountId, role->roleId,
                                                    &practise))
    {
        return false;
    }
    vm_net_mock_offline_practise_normalize_day(&practise);
    practise.isGold = isGold ? 1u : 0u;
    return vm_net_mock_role_offline_practise_save_for(accountId, role->roleId,
                                                      &practise);
}

static u8 vm_net_mock_role_offline_practise_login_flag(void)
{
    return g_vm_net_mock_offline_practise_login_flag;
}

static const char *vm_net_mock_role_offline_practise_login_info(void)
{
    return g_vm_net_mock_offline_practise_login_info;
}

static void vm_net_mock_role_offline_practise_clear_login_notice(void)
{
    g_vm_net_mock_offline_practise_login_flag = 0;
    g_vm_net_mock_offline_practise_login_info[0] = 0;
}

static void vm_net_mock_role_db_load(void)
{
    char path[128];
    u8 fileBuf[sizeof(vm_net_mock_role_db_file)];
    vm_net_mock_role_db_file loaded;
    vm_net_mock_role_db_file_v4 shopWcoinFile;
    vm_net_mock_role_db_file_v3 equippedBackpackFile;
    vm_net_mock_role_db_file_v2 backpackFile;
    vm_net_mock_role_db_file_v1 legacy;
    bool loadedFromFile = false;
    bool loadedFromMysql = false;
    bool loadedFromPayload = false;
    bool needsSave = false;
    u32 migratedOldIds[VM_NET_MOCK_ROLE_DB_MAX_ROLES];
    u32 migratedNewIds[VM_NET_MOCK_ROLE_DB_MAX_ROLES];
    u32 migratedIdCount = 0;

    if (g_vm_net_mock_role_db_loaded)
        return;
    g_vm_net_mock_role_db_loaded = true;
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    memcpy(g_vm_net_mock_role_db.magic, "JHR1", 4);
    g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
    g_vm_net_mock_role_db.activeRoleId = 0;
    g_vm_net_mock_role_db.roleCount = 0;

    memset(migratedOldIds, 0, sizeof(migratedOldIds));
    memset(migratedNewIds, 0, sizeof(migratedNewIds));
    if (!vm_net_mock_role_db_load_mysql_relational(&loadedFromMysql))
    {
        vm_autotest_note("mock_role_db_mysql_load_failed account=%s storage=relational error=%s\n",
                         g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
                         vm_mysql_last_error());
        g_vm_net_mock_role_db_valid = false;
        return;
    }
    if (!loadedFromMysql && !vm_mock_service_mysql_authority_is_sealed() &&
        !vm_net_mock_role_db_load_mysql_payload_backup(&loadedFromPayload))
    {
        vm_autotest_note("mock_role_db_mysql_load_failed account=%s storage=payload-backup error=%s\n",
                         g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
                         vm_mysql_last_error());
        g_vm_net_mock_role_db_valid = false;
        return;
    }

    vm_net_mock_role_db_path(path, sizeof(path));
    if (path[0] == 0)
    {
        g_vm_net_mock_role_db_valid = false;
        return;
    }
    FILE *fp = (loadedFromMysql || loadedFromPayload ||
                vm_mock_service_mysql_authority_is_sealed())
                   ? NULL
                   : fopen(path, "rb");
    if (fp)
    {
        size_t readLen = fread(fileBuf, 1, sizeof(fileBuf), fp);
        fclose(fp);
        if (readLen == sizeof(loaded))
            memcpy(&loaded, fileBuf, sizeof(loaded));
        if (readLen == sizeof(loaded) &&
            memcmp(loaded.magic, "JHR1", 4) == 0 &&
            loaded.version == VM_NET_MOCK_ROLE_DB_VERSION &&
            loaded.roleCount <= VM_NET_MOCK_ROLE_DB_MAX_ROLES)
        {
            g_vm_net_mock_role_db = loaded;
            loadedFromFile = true;
        }
        else if (readLen == sizeof(equippedBackpackFile))
        {
            memcpy(&equippedBackpackFile, fileBuf, sizeof(equippedBackpackFile));
            if (memcmp(equippedBackpackFile.magic, "JHR1", 4) == 0 &&
                equippedBackpackFile.version == VM_NET_MOCK_ROLE_DB_EQUIP_VERSION &&
                equippedBackpackFile.roleCount <= VM_NET_MOCK_ROLE_DB_MAX_ROLES)
            {
                memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
                memcpy(g_vm_net_mock_role_db.magic, "JHR1", 4);
                g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
                g_vm_net_mock_role_db.activeRoleId = equippedBackpackFile.activeRoleId;
                g_vm_net_mock_role_db.roleCount = equippedBackpackFile.roleCount;
                for (u32 i = 0; i < equippedBackpackFile.roleCount; ++i)
                    vm_net_mock_role_copy_from_v3(&g_vm_net_mock_role_db.roles[i],
                                                  &equippedBackpackFile.roles[i]);
                loadedFromFile = true;
                needsSave = true;
                vm_autotest_note("mock_role_db_migrate version=3->5 roles=%u active=%u\n",
                                 g_vm_net_mock_role_db.roleCount,
                                 g_vm_net_mock_role_db.activeRoleId);
            }
        }
        else if (readLen == sizeof(shopWcoinFile))
        {
            memcpy(&shopWcoinFile, fileBuf, sizeof(shopWcoinFile));
            if (memcmp(shopWcoinFile.magic, "JHR1", 4) == 0 &&
                shopWcoinFile.version == VM_NET_MOCK_ROLE_DB_SHOP_WCOIN_VERSION &&
                shopWcoinFile.roleCount <= VM_NET_MOCK_ROLE_DB_MAX_ROLES)
            {
                memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
                memcpy(g_vm_net_mock_role_db.magic, "JHR1", 4);
                g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
                g_vm_net_mock_role_db.activeRoleId = shopWcoinFile.activeRoleId;
                g_vm_net_mock_role_db.roleCount = shopWcoinFile.roleCount;
                for (u32 i = 0; i < shopWcoinFile.roleCount; ++i)
                    vm_net_mock_role_copy_from_v4(&g_vm_net_mock_role_db.roles[i],
                                                  &shopWcoinFile.roles[i]);
                loadedFromFile = true;
                needsSave = true;
                vm_autotest_note("mock_role_db_migrate version=4->5 roles=%u active=%u\n",
                                 g_vm_net_mock_role_db.roleCount,
                                 g_vm_net_mock_role_db.activeRoleId);
            }
        }
        else if (readLen == sizeof(backpackFile))
        {
            memcpy(&backpackFile, fileBuf, sizeof(backpackFile));
            if (memcmp(backpackFile.magic, "JHR1", 4) == 0 &&
                backpackFile.version == VM_NET_MOCK_ROLE_DB_BACKPACK_VERSION &&
                backpackFile.roleCount <= VM_NET_MOCK_ROLE_DB_MAX_ROLES)
            {
                memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
                memcpy(g_vm_net_mock_role_db.magic, "JHR1", 4);
                g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
                g_vm_net_mock_role_db.activeRoleId = backpackFile.activeRoleId;
                g_vm_net_mock_role_db.roleCount = backpackFile.roleCount;
                for (u32 i = 0; i < backpackFile.roleCount; ++i)
                    vm_net_mock_role_copy_from_v2(&g_vm_net_mock_role_db.roles[i],
                                                  &backpackFile.roles[i]);
                loadedFromFile = true;
                needsSave = true;
                vm_autotest_note("mock_role_db_migrate version=2->5 roles=%u active=%u\n",
                                 g_vm_net_mock_role_db.roleCount,
                                 g_vm_net_mock_role_db.activeRoleId);
            }
        }
        else if (readLen == sizeof(legacy))
        {
            memcpy(&legacy, fileBuf, sizeof(legacy));
            if (memcmp(legacy.magic, "JHR1", 4) == 0 &&
                legacy.version == VM_NET_MOCK_ROLE_DB_LEGACY_VERSION &&
                legacy.roleCount <= VM_NET_MOCK_ROLE_DB_MAX_ROLES)
            {
                memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
                memcpy(g_vm_net_mock_role_db.magic, "JHR1", 4);
                g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
                g_vm_net_mock_role_db.activeRoleId = legacy.activeRoleId;
                g_vm_net_mock_role_db.roleCount = legacy.roleCount;
                for (u32 i = 0; i < legacy.roleCount; ++i)
                    vm_net_mock_role_copy_from_v1(&g_vm_net_mock_role_db.roles[i],
                                                  &legacy.roles[i]);
                loadedFromFile = true;
                needsSave = true;
                vm_autotest_note("mock_role_db_migrate version=1->5 roles=%u active=%u\n",
                                 g_vm_net_mock_role_db.roleCount,
                                 g_vm_net_mock_role_db.activeRoleId);
            }
        }
    }

    if (loadedFromFile)
    {
        /* The old file is read only as a one-time import source. */
        needsSave = true;
    }
    else if (loadedFromPayload)
    {
        needsSave = true;
    }
    else if (!loadedFromMysql)
    {
        needsSave = true;
    }

    if (g_vm_net_mock_role_db.roleCount > VM_NET_MOCK_ROLE_DB_MAX_ROLES)
    {
        memset(g_vm_net_mock_role_db.roles, 0, sizeof(g_vm_net_mock_role_db.roles));
        g_vm_net_mock_role_db.roleCount = 0;
        g_vm_net_mock_role_db.activeRoleId = 0;
        needsSave = true;
    }
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        vm_net_mock_role_normalize(&g_vm_net_mock_role_db.roles[i]);
    if (g_vm_net_mock_role_db.roleCount == 1 &&
        vm_net_mock_role_is_pristine_bootstrap_default(&g_vm_net_mock_role_db.roles[0]))
    {
        memset(g_vm_net_mock_role_db.roles, 0, sizeof(g_vm_net_mock_role_db.roles));
        g_vm_net_mock_role_db.roleCount = 0;
        g_vm_net_mock_role_db.activeRoleId = 0;
        needsSave = true;
        vm_autotest_note("mock_role_db_drop_bootstrap_default active=%u\n",
                         g_vm_net_mock_role_db.activeRoleId);
    }
    u32 repairedDefaultNames = vm_net_mock_role_db_repair_duplicate_default_names();
    if (repairedDefaultNames > 0)
    {
        needsSave = true;
        vm_autotest_note("mock_role_db_repair_duplicate_default_names count=%u roles=%u active=%u\n",
                         repairedDefaultNames,
                         g_vm_net_mock_role_db.roleCount,
                         g_vm_net_mock_role_db.activeRoleId);
    }

    if (loadedFromFile || loadedFromPayload)
    {
        u32 oldActiveRoleId = g_vm_net_mock_role_db.activeRoleId;
        for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        {
            u32 newRoleId = 0;
            migratedOldIds[migratedIdCount] = g_vm_net_mock_role_db.roles[i].roleId;
            if (!vm_net_mock_allocate_global_role_id(&newRoleId))
            {
                vm_autotest_note("mock_role_id_global_migrate_failed account=%s old=%u error=%s\n",
                                 g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
                                 migratedOldIds[migratedIdCount], vm_mysql_last_error());
                g_vm_net_mock_role_db_valid = false;
                return;
            }
            migratedNewIds[migratedIdCount] = newRoleId;
            g_vm_net_mock_role_db.roles[i].roleId = newRoleId;
            if (oldActiveRoleId == migratedOldIds[migratedIdCount])
                g_vm_net_mock_role_db.activeRoleId = newRoleId;
            ++migratedIdCount;
        }
        needsSave = true;
    }

    bool activeFound = false;
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == g_vm_net_mock_role_db.activeRoleId)
        {
            activeFound = true;
            break;
        }
    }
    if (!activeFound && g_vm_net_mock_role_db.roleCount > 0)
    {
        g_vm_net_mock_role_db.activeRoleId = g_vm_net_mock_role_db.roles[0].roleId;
        needsSave = true;
    }
    else if (g_vm_net_mock_role_db.roleCount == 0 && g_vm_net_mock_role_db.activeRoleId != 0)
    {
        g_vm_net_mock_role_db.activeRoleId = 0;
        needsSave = true;
    }
    if (g_vm_net_mock_role_db_format_upgraded)
        needsSave = true;
    g_vm_net_mock_role_db_valid = true;
    if (needsSave)
    {
        const char *saveReason = loadedFromPayload ? "payload-relational-migrate" :
                                 loadedFromFile ? "legacy-relational-migrate" :
                                 loadedFromMysql ? "normalize" : "init";
        if (!vm_net_mock_role_db_save_relational(saveReason,
                                                 migratedOldIds,
                                                 migratedNewIds,
                                                 migratedIdCount, true, NULL))
        {
            g_vm_net_mock_role_db_valid = false;
            return;
        }
        if (migratedIdCount > 0)
        {
            vm_net_mock_apply_role_id_migration_to_friend_cache(
                g_vm_mock_service_active_account_id,
                migratedOldIds, migratedNewIds, migratedIdCount);
            for (u32 i = 0; i < migratedIdCount; ++i)
            {
                vm_autotest_note("mock_role_id_global_migrate account=%s old=%u new=%u\n",
                                 g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
                                 migratedOldIds[i], migratedNewIds[i]);
            }
        }
    }
    vm_autotest_note("mock_role_db_mysql_load account=%s source=%s roles=%u active=%u\n",
                     g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
                     loadedFromMysql ? "relational" : loadedFromPayload ? "payload-migrate" :
                     loadedFromFile ? "legacy-migrate" : "init",
                     g_vm_net_mock_role_db.roleCount,
                     g_vm_net_mock_role_db.activeRoleId);
}

static vm_net_mock_role_state *vm_net_mock_active_role(void)
{
    vm_net_mock_role_db_load();
    if (!g_vm_net_mock_role_db_valid || g_vm_net_mock_role_db.roleCount == 0)
        return NULL;
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == g_vm_net_mock_role_db.activeRoleId)
            return &g_vm_net_mock_role_db.roles[i];
    }
    g_vm_net_mock_role_db.activeRoleId = g_vm_net_mock_role_db.roles[0].roleId;
    return &g_vm_net_mock_role_db.roles[0];
}

static bool vm_net_mock_parse_u32_strict(const char *text, u32 *valueOut)
{
    char *end = NULL;
    unsigned long value = 0;

    if (valueOut)
        *valueOut = 0;
    if (text == NULL || text[0] == 0)
        return false;
    value = strtoul(text, &end, 10);
    if (end == NULL || *end != 0 || value > 0xfffffffful)
        return false;
    if (valueOut)
        *valueOut = (u32)value;
    return true;
}

static bool vm_net_mock_parse_i32_strict(const char *text, int32_t *valueOut)
{
    char *end = NULL;
    long value = 0;

    if (valueOut)
        *valueOut = 0;
    if (text == NULL || text[0] == 0)
        return false;
    value = strtol(text, &end, 10);
    if (end == NULL || end == text || *end != 0 ||
        value < (long)INT32_MIN || value > (long)INT32_MAX)
        return false;
    if (valueOut)
        *valueOut = (int32_t)value;
    return true;
}

static vm_net_mock_role_state *vm_net_mock_find_role_in_db(vm_net_mock_role_db_file *db,
                                                           const char *selector)
{
    u32 roleId = 0;

    if (db == NULL || db->roleCount == 0)
        return NULL;
    if (selector == NULL || selector[0] == 0 || strcmp(selector, "active") == 0)
    {
        for (u32 i = 0; i < db->roleCount; ++i)
        {
            if (db->roles[i].roleId == db->activeRoleId)
                return &db->roles[i];
        }
        return &db->roles[0];
    }
    if (vm_net_mock_parse_u32_strict(selector, &roleId))
    {
        for (u32 i = 0; i < db->roleCount; ++i)
        {
            if (db->roles[i].roleId == roleId)
                return &db->roles[i];
        }
    }
    for (u32 i = 0; i < db->roleCount; ++i)
    {
        if (strcmp(db->roles[i].name, selector) == 0)
            return &db->roles[i];
    }
    return NULL;
}

static u32 vm_net_mock_role_wcoin_balance(const vm_net_mock_role_state *role)
{
    return role ? role->wcoin : 0;
}

static u32 vm_net_mock_role_add_wcoin(vm_net_mock_role_state *role, u32 amount)
{
    uint64_t total = 0;

    if (role == NULL || amount == 0)
        return role ? role->wcoin : 0;
    total = (uint64_t)role->wcoin + (uint64_t)amount;
    role->wcoin = total > 0xffffffffull ? 0xffffffffu : (u32)total;
    return role->wcoin;
}

static bool vm_net_mock_select_active_role(u32 roleId)
{
    vm_net_mock_role_db_load();
    if (!g_vm_net_mock_role_db_valid || roleId == 0)
        return false;
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == roleId)
        {
            bool relocated = false;

            if (g_vm_net_mock_role_db.activeRoleId != roleId)
            {
                u32 previousActiveRoleId = g_vm_net_mock_role_db.activeRoleId;
                g_vm_net_mock_role_db.activeRoleId = roleId;
                if (!vm_net_mock_role_db_save("role-select"))
                {
                    g_vm_net_mock_role_db.activeRoleId = previousActiveRoleId;
                    return false;
                }
            }
            /*
             * Only at role-select (not every normalize): leave 29* when the
             * dream SCE has kind=3 so cold login skips ParseMinfo; instance
             * enter later may set scene back to 29* for ??.
             */
            relocated = vm_net_mock_role_relocate_dream_kind3_login(
                &g_vm_net_mock_role_db.roles[i]);
            if (relocated)
                (void)vm_net_mock_role_db_save("dream-kind3-login-relocate");
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_role_db_has_role_id(u32 roleId)
{
    vm_net_mock_role_db_load();
    if (!g_vm_net_mock_role_db_valid || roleId == 0)
        return false;
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == roleId)
            return true;
    }
    return false;
}

static u8 vm_net_mock_active_role_job_or(u8 fallback)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (role == NULL || role->job == 0 || role->job > 3)
        return fallback;
    return role->job;
}

static u8 vm_net_mock_active_role_sex_or(u8 fallback)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (role == NULL || role->sex > 1)
        return fallback;
    return role->sex;
}

static u8 vm_net_mock_role_db_sex_from_title_value(u8 rawSex)
{
    if (rawSex >= 1 && rawSex <= 2)
        return (u8)(rawSex - 1);
    if (rawSex == 0)
        return 0;
    return 0;
}

static u8 vm_net_mock_role_db_job_from_title_value(u8 rawJob, bool rawJobIsIndex)
{
    if (rawJobIsIndex)
    {
        if (rawJob <= 2)
            return (u8)(rawJob + 1);
        return 1;
    }
    if (rawJob >= 1 && rawJob <= 3)
        return rawJob;
    if (rawJob == 0)
        return 1;
    return 1;
}

static u32 vm_net_mock_role_db_next_role_id(void)
{
    u32 nextRoleId = 0;
    vm_net_mock_role_db_load();
    if (!g_vm_net_mock_role_db_valid || !vm_net_mock_allocate_global_role_id(&nextRoleId))
        return 0;
    return nextRoleId;
}

static void vm_net_mock_copy_role_name(char *out, size_t outCap, const char *name)
{
    size_t copyLen = 0;
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (name != NULL)
    {
        copyLen = strlen(name);
        if (copyLen >= outCap)
            copyLen = outCap - 1;
        while (copyLen > 0 && name[copyLen - 1] == 0)
            --copyLen;
        if (copyLen > 0)
            memcpy(out, name, copyLen);
        out[copyLen] = 0;
    }
    if (out[0] == 0)
        snprintf(out, outCap, "%s", vm_net_mock_default_role_name());
}

static bool vm_net_mock_role_db_create_from_title(const vm_net_mock_title_role_create_request *request,
                                                  u32 *actorIdOut,
                                                  u8 *resultOut)
{
    vm_net_mock_role_state *role = NULL;
    u32 actorId = 0;
    u32 previousActiveRoleId = 0;
    u8 result = 1;

    if (actorIdOut)
        *actorIdOut = 0;
    if (resultOut)
        *resultOut = result;
    if (request == NULL)
        return false;

    vm_net_mock_role_db_load();
    if (!g_vm_net_mock_role_db_valid ||
        g_vm_net_mock_role_db.roleCount >= VM_NET_MOCK_ROLE_DB_MAX_ROLES)
    {
        return true;
    }

    actorId = vm_net_mock_role_db_next_role_id();
    if (actorId == 0)
        return true;
    previousActiveRoleId = g_vm_net_mock_role_db.activeRoleId;
    role = &g_vm_net_mock_role_db.roles[g_vm_net_mock_role_db.roleCount];
    vm_net_mock_role_init_default(role);
    role->roleId = actorId;
    if (request->name[0] != 0)
        vm_net_mock_copy_role_name(role->name, sizeof(role->name), request->name);
    else
        vm_net_mock_role_assign_fallback_name(role);
    role->job = vm_net_mock_role_db_job_from_title_value(request->rawJob, request->rawJobIsIndex);
    role->sex = vm_net_mock_role_db_sex_from_title_value(request->rawSex);
    vm_net_mock_role_init_default_equipment(role);
    vm_net_mock_role_normalize(role);
    role->hp = role->hpMax;
    role->mp = role->mpMax;

    g_vm_net_mock_role_db.roleCount += 1;
    g_vm_net_mock_role_db.activeRoleId = actorId;
    if (!vm_net_mock_role_db_save_relational("role-create", NULL, NULL, 0, true, NULL))
    {
        --g_vm_net_mock_role_db.roleCount;
        memset(role, 0, sizeof(*role));
        g_vm_net_mock_role_db.activeRoleId = previousActiveRoleId;
        return true;
    }

    result = 0;
    if (actorIdOut)
        *actorIdOut = actorId;
    if (resultOut)
        *resultOut = result;
    return true;
}

static bool vm_net_mock_role_db_delete_by_id(u32 actorId, u8 *resultOut, u32 *roleCountOut)
{
    u8 result = 1;
    u32 deleteIndex = VM_NET_MOCK_ROLE_DB_MAX_ROLES;
    vm_net_mock_role_db_file before;

    if (resultOut)
        *resultOut = result;
    if (roleCountOut)
        *roleCountOut = 0;
    if (actorId == 0)
        return false;

    vm_net_mock_role_db_load();
    if (!g_vm_net_mock_role_db_valid)
        return false;

    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        if (g_vm_net_mock_role_db.roles[i].roleId == actorId)
        {
            deleteIndex = i;
            break;
        }
    }

    if (deleteIndex == VM_NET_MOCK_ROLE_DB_MAX_ROLES)
    {
        if (roleCountOut)
            *roleCountOut = g_vm_net_mock_role_db.roleCount;
        return true;
    }

    before = g_vm_net_mock_role_db;

    for (u32 i = deleteIndex; i + 1 < g_vm_net_mock_role_db.roleCount; ++i)
        g_vm_net_mock_role_db.roles[i] = g_vm_net_mock_role_db.roles[i + 1];
    if (g_vm_net_mock_role_db.roleCount > 0)
    {
        memset(&g_vm_net_mock_role_db.roles[g_vm_net_mock_role_db.roleCount - 1],
               0,
               sizeof(g_vm_net_mock_role_db.roles[g_vm_net_mock_role_db.roleCount - 1]));
        g_vm_net_mock_role_db.roleCount -= 1;
    }

    if (g_vm_net_mock_role_db.roleCount == 0)
    {
        g_vm_net_mock_role_db.activeRoleId = 0;
    }
    else if (g_vm_net_mock_role_db.activeRoleId == actorId)
    {
        g_vm_net_mock_role_db.activeRoleId = g_vm_net_mock_role_db.roles[0].roleId;
    }

    if (!vm_net_mock_role_db_save_relational("role-delete", NULL, NULL, 0, true, NULL))
    {
        g_vm_net_mock_role_db = before;
        if (roleCountOut)
            *roleCountOut = before.roleCount;
        return false;
    }
    result = 0;
    if (resultOut)
        *resultOut = result;
    if (roleCountOut)
        *roleCountOut = g_vm_net_mock_role_db.roleCount;
    return true;
}

static void vm_net_mock_role_set_position(const char *scene, u16 x, u16 y, const char *reason)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_role_state before;
    if (role == NULL || scene == NULL || scene[0] == 0 || x == 0 || y == 0)
        return;
    /* Scene bootstrap reports the already persisted landing point again after
     * the map has finished initializing.  Rewriting the complete relational
     * role/equipment/backpack snapshot for that no-op costs several MySQL
     * round trips while the protocol state lock is held, delaying the very
     * response that contains the first NPC catalog and welcome message. */
    if (role->x == x && role->y == y &&
        vm_net_mock_scene_name_is_persistable(role->scene) &&
        vm_net_mock_scene_names_equal_loose(role->scene, scene))
    {
        printf("[debug][mock-service] role_position_save_skip role=%u scene=%s pos=(%u,%u) reason=%s unchanged=1\n",
               role->roleId, scene, x, y, reason ? reason : "position");
        return;
    }
    before = *role;
    snprintf(role->scene, sizeof(role->scene), "%s", scene);
    role->x = x;
    role->y = y;
    vm_net_mock_role_normalize(role);
    if (!vm_net_mock_role_db_save(reason ? reason : "position"))
    {
        *role = before;
        printf("[error][mock-service] role_position_persist_failed role=%u reason=%s error=%s\n",
               before.roleId, reason ? reason : "position", vm_mysql_last_error());
    }
}

/* A 2/1 direction timeline is already an acknowledged movement boundary for
 * the remote client.  Do not defer its durability to a best-effort disconnect:
 * Android force-stop cannot emit CBMS CLIENT_DISCONNECT.  This intentionally
 * updates only the active role row rather than calling role_db_save(), whose
 * complete snapshot reconciliation deletes/reinserts backpack and equipment
 * rows and is not a valid per-movement transaction. */
static bool vm_net_mock_role_commit_timeline_position(const vm_net_mock_role_state *role,
                                                       const char *reason)
{
    char accountHex[129];
    char sceneHex[sizeof(role->scene) * 2 + 1];
    char query[512];
    size_t sceneLen = 0;

    if (role == NULL || role->roleId == 0 ||
        !vm_net_mock_scene_name_is_persistable(role->scene) ||
        role->x == 0 || role->y == 0 ||
        !vm_net_mock_mysql_account_hex(accountHex))
    {
        return false;
    }
    sceneLen = vm_mock_mysql_bounded_strlen(role->scene, sizeof(role->scene));
    if (sceneLen == 0 || sceneLen >= sizeof(role->scene) ||
        vm_mysql_hex_encode(role->scene, sceneLen, sceneHex, sizeof(sceneHex)) == 0)
    {
        return false;
    }
    snprintf(query, sizeof(query),
             "UPDATE account_roles SET scene=X'%s',pos_x=%u,pos_y=%u "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
             sceneHex, role->x, role->y, accountHex, role->roleId);
    if (!vm_mysql_exec(query))
    {
        printf("[error][mock-service] role_timeline_position_persist_failed "
               "account=%s role=%u scene=%s pos=(%u,%u) reason=%s error=%s\n",
               g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
               role->roleId, role->scene, role->x, role->y,
               reason ? reason : "moveinfo-timeline", vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_role_position_dirty = false;
    /*
     * Narrow position UPDATE is still a newer durable authority than a queued
     * deferred snapshot that may carry the pre-move scene/pos.  Inventory may
     * remain dirty; note_durable keeps that bit and asks for reflush.
     */
    vm_mock_service_account_note_durable_role_write(
        g_vm_mock_service_active_account_id,
        &g_vm_net_mock_role_db,
        g_vm_net_mock_role_db_valid,
        NULL,
        false,
        true,
        reason ? reason : "moveinfo-timeline");
    return true;
}

static bool vm_net_mock_role_set_timeline_position(const char *scene,
                                                   u16 x,
                                                   u16 y,
                                                   const char *reason)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_role_state before;
    bool dirtyBefore = g_vm_net_mock_role_position_dirty;

    if (role == NULL || !vm_net_mock_scene_name_is_persistable(scene) || x == 0 || y == 0)
        return false;
    /*
     * A movement timeline starts from the session's already validated scene
     * position and applies only the client's exact 4-pixel direction steps.
     * Re-running SCE landing/portal normalization here is both semantically
     * wrong (this is not a scene landing) and expensive on every upload.
     */
    before = *role;
    snprintf(role->scene, sizeof(role->scene), "%s", scene);
    role->x = x;
    role->y = y;
    g_vm_net_mock_role_position_dirty = true;
    if (!vm_net_mock_role_commit_timeline_position(role, reason))
    {
        *role = before;
        g_vm_net_mock_role_position_dirty = dirtyBefore;
        return false;
    }
    return true;
}

/*
 * Player warehouse is relational state outside the binary role snapshot, same
 * rationale as timed item effects.  UI entry is mall durable pass 834 + the
 * proven 26/1 NPC-service dialog path (??/??), not unresolved ?? WT.
 */
static bool vm_net_mock_warehouse_prepare_schema(void)
{
    if (g_vm_net_mock_warehouse_schema_prepared)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_role_warehouse ("
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "slot_index SMALLINT UNSIGNED NOT NULL,"
            "item_id INT UNSIGNED NOT NULL,"
            "item_seq SMALLINT UNSIGNED NOT NULL,"
            "item_count INT UNSIGNED NOT NULL,"
            "enhance_level SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "enhance_wash VARCHAR(96) NOT NULL DEFAULT "
            "'0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(account_id,role_id,slot_index),"
            "KEY idx_account_role_warehouse_item(account_id,role_id,item_id,item_seq),"
            "CONSTRAINT fk_account_role_warehouse_role FOREIGN KEY(account_id,role_id) "
            "REFERENCES account_roles(account_id,role_id) ON DELETE CASCADE"
            ") ENGINE=InnoDB"))
    {
        printf("[error][network] mock_warehouse_schema_prepare error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    /* Gold bank is separate from item slots so empty warehouses still keep
     * stored copper, and item DELETE/INSERT cannot wipe money. */
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_role_warehouse_bank ("
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "money INT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(account_id,role_id),"
            "CONSTRAINT fk_account_role_warehouse_bank_role FOREIGN KEY(account_id,role_id) "
            "REFERENCES account_roles(account_id,role_id) ON DELETE CASCADE"
            ") ENGINE=InnoDB"))
    {
        printf("[error][network] mock_warehouse_bank_schema_prepare error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_warehouse_schema_prepared = true;
    return true;
}

typedef struct
{
    vm_net_mock_warehouse_state *state;
    bool invalid;
} vm_net_mock_warehouse_load_context;

static bool vm_net_mock_warehouse_load_row(void *context_value,
                                           unsigned int column_count,
                                           const char *const *values,
                                           const size_t *lengths)
{
    vm_net_mock_warehouse_load_context *context =
        (vm_net_mock_warehouse_load_context *)context_value;
    u32 slot = 0;
    u32 itemId = 0;
    u32 itemSeq = 0;
    u32 itemCount = 0;
    u32 enhance = 0;

    if (context == NULL || context->state == NULL ||
        (column_count != 5 && column_count != 6) ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &slot) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &itemId) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &itemSeq) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &itemCount) ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &enhance) ||
        slot >= VM_NET_MOCK_WAREHOUSE_MAX_ITEMS || itemId == 0 ||
        itemCount == 0 || itemSeq > 0xffffu || enhance > 0xffffu)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    if (context->state->itemCount < VM_NET_MOCK_WAREHOUSE_MAX_ITEMS)
    {
        vm_net_mock_backpack_item_state *item =
            &context->state->items[context->state->itemCount++];
        memset(item, 0, sizeof(*item));
        item->itemId = itemId;
        item->seq = (u16)itemSeq;
        item->count = itemCount;
        item->enhanceLevel = (u16)enhance;
        vm_net_mock_enhance_wash_clear(&item->enhanceWash);
        if (column_count == 6 && values[5] != NULL)
            (void)vm_net_mock_enhance_wash_decode(values[5], lengths[5],
                                                  &item->enhanceWash);
        if (item->seq >= context->state->nextSeq)
            context->state->nextSeq = (u16)(item->seq + 1u);
    }
    return true;
}

typedef struct
{
    u32 money;
    bool found;
    bool invalid;
} vm_net_mock_warehouse_bank_load_context;

static bool vm_net_mock_warehouse_bank_load_row(void *context_value,
                                                unsigned int column_count,
                                                const char *const *values,
                                                const size_t *lengths)
{
    vm_net_mock_warehouse_bank_load_context *context =
        (vm_net_mock_warehouse_bank_load_context *)context_value;
    u32 money = 0;

    if (context == NULL || column_count < 1 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &money))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->money = money;
    context->found = true;
    return true;
}

static bool vm_net_mock_warehouse_load_for_role(vm_net_mock_role_state *role)
{
    char account_hex[129];
    char query[384];
    vm_net_mock_warehouse_load_context context;
    vm_net_mock_warehouse_bank_load_context bankContext;
    const char *account_id = g_vm_mock_service_active_account_id;

    if (role == NULL || role->roleId == 0 || account_id == NULL || account_id[0] == 0)
        return false;
    if (g_vm_net_mock_warehouse.loaded &&
        g_vm_net_mock_warehouse.roleId == role->roleId &&
        strcmp(g_vm_net_mock_warehouse.accountId, account_id) == 0)
    {
        return true;
    }
    if (!vm_net_mock_warehouse_prepare_schema() ||
        !vm_net_mock_warehouse_ensure_enhance_wash_column() ||
        !vm_net_mock_mysql_account_hex(account_hex))
    {
        return false;
    }

    memset(&g_vm_net_mock_warehouse, 0, sizeof(g_vm_net_mock_warehouse));
    snprintf(g_vm_net_mock_warehouse.accountId,
             sizeof(g_vm_net_mock_warehouse.accountId), "%s", account_id);
    g_vm_net_mock_warehouse.roleId = role->roleId;
    g_vm_net_mock_warehouse.nextSeq = 1;
    context.state = &g_vm_net_mock_warehouse;
    context.invalid = false;
    snprintf(query, sizeof(query),
             "SELECT slot_index,item_id,item_seq,item_count,enhance_level,enhance_wash "
             "FROM account_role_warehouse WHERE account_id=CAST(X'%s' AS CHAR) "
             "AND role_id=%u ORDER BY slot_index",
             account_hex, role->roleId);
    if (!vm_mysql_query(query, vm_net_mock_warehouse_load_row, &context) ||
        context.invalid)
    {
        printf("[error][network] mock_warehouse_load role=%u error=%s invalid=%u\n",
               role->roleId, vm_mysql_last_error(), context.invalid ? 1u : 0u);
        memset(&g_vm_net_mock_warehouse, 0, sizeof(g_vm_net_mock_warehouse));
        return false;
    }
    memset(&bankContext, 0, sizeof(bankContext));
    snprintf(query, sizeof(query),
             "SELECT money FROM account_role_warehouse_bank "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
             account_hex, role->roleId);
    if (!vm_mysql_query(query, vm_net_mock_warehouse_bank_load_row, &bankContext) ||
        bankContext.invalid)
    {
        printf("[error][network] mock_warehouse_bank_load role=%u error=%s "
               "invalid=%u\n",
               role->roleId, vm_mysql_last_error(),
               bankContext.invalid ? 1u : 0u);
        memset(&g_vm_net_mock_warehouse, 0, sizeof(g_vm_net_mock_warehouse));
        return false;
    }
    /* Drop residual copper so only whole 金 can leave the bank. */
    g_vm_net_mock_warehouse.money =
        (bankContext.money / VM_NET_MOCK_COPPER_PER_GOLD) *
        VM_NET_MOCK_COPPER_PER_GOLD;
    if (g_vm_net_mock_warehouse.nextSeq == 0)
        g_vm_net_mock_warehouse.nextSeq = 1;
    g_vm_net_mock_warehouse.loaded = true;
    printf("[info][network] mock_warehouse_load role=%u slots=%u money=%u "
           "gold=%u\n",
           role->roleId, g_vm_net_mock_warehouse.itemCount,
           g_vm_net_mock_warehouse.money,
           g_vm_net_mock_warehouse.money / VM_NET_MOCK_COPPER_PER_GOLD);
    return true;
}

static bool vm_net_mock_warehouse_write_sql(const vm_net_mock_role_state *role,
                                            const char *account_hex,
                                            const vm_net_mock_warehouse_state *warehouse,
                                            char *mysql_error,
                                            size_t mysql_error_cap)
{
    char *query = NULL;
    const size_t queryCap = 65536;
    u32 queryLen = 0;
    u32 slot = 0;
    u32 prefixLen = 0;
    u32 valueCount = 0;
    bool ok = false;

    if (mysql_error != NULL && mysql_error_cap > 0)
        mysql_error[0] = 0;
    if (role == NULL || account_hex == NULL || warehouse == NULL ||
        !warehouse->loaded || warehouse->roleId != role->roleId)
    {
        return true;
    }
    if (!vm_net_mock_warehouse_prepare_schema() ||
        !vm_net_mock_warehouse_ensure_enhance_wash_column())
    {
        if (mysql_error != NULL && mysql_error_cap > 0)
            snprintf(mysql_error, mysql_error_cap, "%s", vm_mysql_last_error());
        return false;
    }

    query = (char *)malloc(queryCap);
    if (query == NULL)
    {
        if (mysql_error != NULL && mysql_error_cap > 0)
            snprintf(mysql_error, mysql_error_cap, "warehouse query alloc failed");
        return false;
    }

    snprintf(query, queryCap,
             "DELETE FROM account_role_warehouse WHERE account_id=CAST(X'%s' AS CHAR) "
             "AND role_id=%u",
             account_hex, role->roleId);
    if (!vm_mysql_exec(query))
    {
        if (mysql_error != NULL && mysql_error_cap > 0)
            snprintf(mysql_error, mysql_error_cap, "%s", vm_mysql_last_error());
        free(query);
        return false;
    }
    if (warehouse->itemCount != 0)
    {
        prefixLen = (u32)snprintf(
            query, queryCap,
            "INSERT INTO account_role_warehouse(account_id,role_id,slot_index,item_id,"
            "item_seq,item_count,enhance_level,enhance_wash) VALUES");
        if (prefixLen == 0 || prefixLen >= queryCap)
        {
            if (mysql_error != NULL && mysql_error_cap > 0)
                snprintf(mysql_error, mysql_error_cap, "warehouse query too large");
            free(query);
            return false;
        }
        queryLen = prefixLen;
        for (slot = 0; slot < warehouse->itemCount; ++slot)
        {
            const vm_net_mock_backpack_item_state *item = &warehouse->items[slot];
            char washText[VM_NET_MOCK_ENHANCE_WASH_TEXT_MAX];
            int written;

            if (item->itemId == 0 || item->count == 0)
                continue;
            (void)vm_net_mock_enhance_wash_sql_text(&item->enhanceWash, washText,
                                                    sizeof(washText));
            written = snprintf(query + queryLen, queryCap - queryLen,
                               "%s(CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u,%u,'%s')",
                               valueCount > 0u ? "," : "",
                               account_hex, role->roleId, slot, item->itemId,
                               item->seq, item->count, item->enhanceLevel, washText);
            if (written <= 0 || (u32)written >= queryCap - queryLen)
            {
                if (mysql_error != NULL && mysql_error_cap > 0)
                    snprintf(mysql_error, mysql_error_cap, "warehouse query too large");
                free(query);
                return false;
            }
            queryLen += (u32)written;
            ++valueCount;
        }
        if (valueCount != 0 && !vm_mysql_exec(query))
        {
            if (mysql_error != NULL && mysql_error_cap > 0)
                snprintf(mysql_error, mysql_error_cap, "%s", vm_mysql_last_error());
            free(query);
            return false;
        }
    }

    /* Bank money commits with the same warehouse snapshot as item rows. */
    snprintf(query, queryCap,
             "INSERT INTO account_role_warehouse_bank(account_id,role_id,money) "
             "VALUES(CAST(X'%s' AS CHAR),%u,%u) "
             "ON DUPLICATE KEY UPDATE money=VALUES(money)",
             account_hex, role->roleId, warehouse->money);
    ok = vm_mysql_exec(query);
    if (!ok && mysql_error != NULL && mysql_error_cap > 0)
        snprintf(mysql_error, mysql_error_cap, "%s", vm_mysql_last_error());
    free(query);
    return ok;
}

static bool vm_net_mock_warehouse_persist(vm_net_mock_role_state *role)
{
    char account_hex[129];
    char mysql_error[512];

    if (role == NULL || !g_vm_net_mock_warehouse.loaded ||
        g_vm_net_mock_warehouse.roleId != role->roleId)
    {
        return false;
    }
    if (!vm_net_mock_mysql_account_hex(account_hex))
        return false;
    if (!vm_net_mock_warehouse_write_sql(role, account_hex, &g_vm_net_mock_warehouse,
                                         mysql_error, sizeof(mysql_error)))
    {
        printf("[error][network] mock_warehouse_persist role=%u error=%s\n",
               role->roleId, mysql_error[0] ? mysql_error : vm_mysql_last_error());
        return false;
    }
    return true;
}

static vm_net_mock_warehouse_state *vm_net_mock_warehouse_active(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (role == NULL || !vm_net_mock_warehouse_load_for_role(role))
        return NULL;
    return &g_vm_net_mock_warehouse;
}

/*
 * Move whole 金 between role.money and warehouse.money.
 * Deduct source first, then credit destination — never invent copper.
 * Amounts are server-resolved; client only picks preset gold / "all".
 */
static bool vm_net_mock_warehouse_money_transfer(vm_net_mock_role_state *role,
                                                 bool deposit,
                                                 u32 goldAmount,
                                                 bool allGold,
                                                 const char **reasonOut)
{
    vm_net_mock_warehouse_state *warehouse = NULL;
    u32 copper = 0;
    u32 beforeRoleMoney = 0;
    u32 beforeBankMoney = 0;

    if (reasonOut)
        *reasonOut = "ok";
    if (role == NULL)
    {
        if (reasonOut)
            *reasonOut = "bad-args";
        return false;
    }
    warehouse = vm_net_mock_warehouse_active();
    if (warehouse == NULL || !warehouse->loaded)
    {
        if (reasonOut)
            *reasonOut = "warehouse-unavailable";
        return false;
    }

    beforeRoleMoney = role->money;
    beforeBankMoney = warehouse->money;

    if (allGold)
    {
        if (deposit)
            copper = (role->money / VM_NET_MOCK_COPPER_PER_GOLD) *
                     VM_NET_MOCK_COPPER_PER_GOLD;
        else
            copper = (warehouse->money / VM_NET_MOCK_COPPER_PER_GOLD) *
                     VM_NET_MOCK_COPPER_PER_GOLD;
    }
    else
    {
        if (goldAmount == 0)
        {
            if (reasonOut)
                *reasonOut = "bad-amount";
            return false;
        }
        if (goldAmount > 0xffffffffu / VM_NET_MOCK_COPPER_PER_GOLD)
        {
            if (reasonOut)
                *reasonOut = "overflow";
            return false;
        }
        copper = goldAmount * VM_NET_MOCK_COPPER_PER_GOLD;
    }

    if (copper == 0)
    {
        if (reasonOut)
            *reasonOut = deposit ? "no-gold" : "bank-empty";
        return false;
    }

    if (deposit)
    {
        if (role->money < copper)
        {
            if (reasonOut)
                *reasonOut = "money-insufficient";
            return false;
        }
        if (warehouse->money > 0xffffffffu - copper)
        {
            if (reasonOut)
                *reasonOut = "bank-overflow";
            return false;
        }
        role->money -= copper;
        warehouse->money += copper;
    }
    else
    {
        if (warehouse->money < copper)
        {
            if (reasonOut)
                *reasonOut = "bank-insufficient";
            return false;
        }
        if (role->money > 0xffffffffu - copper)
        {
            if (reasonOut)
                *reasonOut = "money-overflow";
            return false;
        }
        warehouse->money -= copper;
        role->money += copper;
    }

    vm_net_mock_role_mark_inventory_dirty(
        deposit ? "warehouse-money-deposit" : "warehouse-money-withdraw");
    printf("[info][network] mock_warehouse_money_%s role=%u gold=%u copper=%u "
           "role_money=%u->%u bank_money=%u->%u all=%u\n",
           deposit ? "deposit" : "withdraw", role->roleId,
           copper / VM_NET_MOCK_COPPER_PER_GOLD, copper, beforeRoleMoney,
           role->money, beforeBankMoney, warehouse->money, allGold ? 1u : 0u);
    return true;
}

static bool vm_net_mock_warehouse_item_is_depositable(u32 itemId)
{
    if (itemId == 0 || itemId == VM_NET_MOCK_WAREHOUSE_PASS_ITEM_ID ||
        itemId == VM_NET_MOCK_EQUIP_SELL_PASS_ITEM_ID)
        return false;
    /* Direct-store mall values never become ordinary backpack rows. */
    if (itemId == 808 || itemId == 817 || itemId == 818 || itemId == 819)
        return false;
    return true;
}

static bool vm_net_mock_equip_sell_item_is_sellable(u32 itemId)
{
    if (itemId == 0 || itemId == VM_NET_MOCK_WAREHOUSE_PASS_ITEM_ID ||
        itemId == VM_NET_MOCK_EQUIP_SELL_PASS_ITEM_ID)
        return false;
    return vm_net_mock_find_equipment_catalog_item(itemId) != NULL;
}

/*
 * Permanent sell: remove one backpack equipment row and credit full shop/equip
 * catalog ?? (not discard's floor(price/10)).
 */
static bool vm_net_mock_equip_sell_backpack_seq(vm_net_mock_role_state *role,
                                                u16 backpackSeq,
                                                u32 *soldPriceOut,
                                                u32 *soldItemIdOut,
                                                const char **reasonOut)
{
    vm_net_mock_backpack_item_state *bagItem = NULL;
    vm_net_mock_role_state beforeRole;
    const vm_net_mock_shop_catalog_item *catalog = NULL;
    u32 remaining = 0;
    u32 price = 0;
    u32 itemId = 0;

    if (reasonOut)
        *reasonOut = "ok";
    if (soldPriceOut)
        *soldPriceOut = 0;
    if (soldItemIdOut)
        *soldItemIdOut = 0;
    if (role == NULL || backpackSeq == 0)
    {
        if (reasonOut)
            *reasonOut = "bad-args";
        return false;
    }
    bagItem = vm_net_mock_role_find_backpack_item(role, 0, backpackSeq);
    if (bagItem == NULL || bagItem->count == 0 ||
        !vm_net_mock_equip_sell_item_is_sellable(bagItem->itemId))
    {
        if (reasonOut)
            *reasonOut = "item-not-sellable";
        return false;
    }
    itemId = bagItem->itemId;
    catalog = vm_net_mock_find_shop_catalog_item(itemId);
    if (catalog == NULL)
    {
        if (reasonOut)
            *reasonOut = "price-unresolved";
        return false;
    }
    price = catalog->price;
    beforeRole = *role;
    if (!vm_net_mock_role_consume_backpack_item(role, itemId, backpackSeq, 1,
                                                &remaining))
    {
        if (reasonOut)
            *reasonOut = "consume-failed";
        return false;
    }
    role->money = vm_net_mock_add_capped_u32(role->money, price);
    (void)beforeRole;
    vm_net_mock_role_mark_inventory_dirty("equip-sell");
    if (soldPriceOut)
        *soldPriceOut = price;
    if (soldItemIdOut)
        *soldItemIdOut = itemId;
    printf("[info][network] mock_equip_sell role=%u bag_seq=%u item=%u "
           "price=%u money=%u bag_rows=%u evidence=full-catalog-value-not-discard-1/10\n",
           role->roleId, backpackSeq, itemId, price, role->money,
           role->backpackItemCount);
    return true;
}

/*
 * item.dsh 828: while battle insight is active, monster drops that cannot
 * enter the bag are auto-sold for catalog copper instead of being lost.
 * All shop-listed rows use the same discard refund floor(price/10).
 */
static u32 vm_net_mock_battle_insight_drop_auto_sell_unit_price(u32 itemId)
{
    const vm_net_mock_shop_catalog_item *catalog = NULL;

    if (itemId == 0)
        return 0;
    catalog = vm_net_mock_find_shop_catalog_item(itemId);
    if (catalog == NULL)
        return 0;
    return catalog->price / 10u;
}

static bool vm_net_mock_battle_insight_try_auto_sell_drop(
    vm_net_mock_role_state *role, u32 itemId, u32 count, u32 *soldGoldOut)
{
    u32 unitPrice = 0;
    u32 sellGold = 0;

    if (soldGoldOut)
        *soldGoldOut = 0;
    if (role == NULL || itemId == 0 || count == 0)
        return false;
    if (vm_net_mock_role_active_battle_exp_bonus_percent(role) == 0)
        return false;
    unitPrice = vm_net_mock_battle_insight_drop_auto_sell_unit_price(itemId);
    if (unitPrice == 0)
        return false;
    sellGold = vm_net_mock_mul_capped_u32(unitPrice, count);
    if (sellGold == 0)
        return false;
    if (soldGoldOut)
        *soldGoldOut = sellGold;
    printf("[info][network] mock_battle_insight_auto_sell role=%u item=%u "
           "count=%u unit_price=%u gold=%u reason=bag-full "
           "evidence=item.dsh:828\n",
           role->roleId, itemId, count, unitPrice, sellGold);
    return true;
}

static bool vm_net_mock_role_grant_flask_reservoir_points(
    vm_net_mock_role_state *role,
    u32 itemId,
    u32 grantPoints,
    u16 *seqOut,
    const char *reason);

/*
 * Restore one mall durable pass (834/839) with exact remaining uses.
 * add_backpack_item_to_role treats count as NEW full-pass quantity (mall buy):
 * count=50 would spawn 50 rows of durability 50. Auction claim / warehouse
 * retrieve store remaining durability in item_count and must use this path.
 */
static bool vm_net_mock_role_grant_durable_pass_remaining(
    vm_net_mock_role_state *role,
    u32 itemId,
    u32 remaining,
    u16 *seqOut,
    const char *reason);

static bool vm_net_mock_backpack_item_id_is_durable_mall_pass(u32 itemId)
{
    return itemId == VM_NET_MOCK_WAREHOUSE_PASS_ITEM_ID ||
           itemId == VM_NET_MOCK_EQUIP_SELL_PASS_ITEM_ID;
}

static bool vm_net_mock_warehouse_deposit_backpack_seq(vm_net_mock_role_state *role,
                                                       u16 backpackSeq,
                                                       const char **reasonOut)
{
    vm_net_mock_warehouse_state *warehouse = NULL;
    vm_net_mock_backpack_item_state *bagItem = NULL;
    vm_net_mock_backpack_item_state moved;
    vm_net_mock_role_state beforeRole;
    vm_net_mock_warehouse_state beforeWarehouse;

    if (reasonOut)
        *reasonOut = "ok";
    if (role == NULL || backpackSeq == 0)
    {
        if (reasonOut)
            *reasonOut = "bad-args";
        return false;
    }
    warehouse = vm_net_mock_warehouse_active();
    if (warehouse == NULL)
    {
        if (reasonOut)
            *reasonOut = "warehouse-unavailable";
        return false;
    }
    bagItem = vm_net_mock_role_find_backpack_item(role, 0, backpackSeq);
    if (bagItem == NULL || bagItem->count == 0 ||
        !vm_net_mock_warehouse_item_is_depositable(bagItem->itemId))
    {
        if (reasonOut)
            *reasonOut = "item-not-depositable";
        return false;
    }
    if (warehouse->itemCount >= VM_NET_MOCK_WAREHOUSE_MAX_ITEMS)
    {
        if (reasonOut)
            *reasonOut = "warehouse-full";
        return false;
    }

    beforeRole = *role;
    beforeWarehouse = *warehouse;
    moved = *bagItem;
    if (vm_net_mock_backpack_item_id_uses_reservoir_count(moved.itemId))
    {
        vm_net_mock_backpack_item_state *whFlask = NULL;
        for (u16 wi = 0; wi < warehouse->itemCount; ++wi)
        {
            if (warehouse->items[wi].itemId == moved.itemId &&
                warehouse->items[wi].count != 0)
            {
                whFlask = &warehouse->items[wi];
                break;
            }
        }
        if (whFlask != NULL)
        {
            if (whFlask->count >= VM_NET_MOCK_FLASK_MAX_RESERVOIR ||
                moved.count > VM_NET_MOCK_FLASK_MAX_RESERVOIR - whFlask->count)
            {
                if (reasonOut)
                    *reasonOut = "flask-reservoir-full";
                return false;
            }
            whFlask->count += moved.count;
            bagItem->count = 0;
            vm_net_mock_role_normalize_backpack(role);
            (void)beforeRole;
            (void)beforeWarehouse;
            vm_net_mock_role_mark_inventory_dirty("warehouse-deposit-flask-merge");
            printf("[info][network] mock_warehouse_deposit role=%u bag_seq=%u "
                   "item=%u enhance=%u warehouse_slots=%u bag_rows=%u "
                   "flask_merge_remaining=%u\n",
                   role->roleId, backpackSeq, moved.itemId, moved.enhanceLevel,
                   warehouse->itemCount, role->backpackItemCount, whFlask->count);
            return true;
        }
    }
    if (warehouse->nextSeq == 0)
        warehouse->nextSeq = 1;
    moved.seq = warehouse->nextSeq++;
    if (warehouse->nextSeq == 0)
        warehouse->nextSeq = 1;
    warehouse->items[warehouse->itemCount++] = moved;
    bagItem->count = 0;
    vm_net_mock_role_normalize_backpack(role);
    /* Memory is authoritative for the session; MySQL flushes after CBMR. */
    (void)beforeRole;
    (void)beforeWarehouse;
    vm_net_mock_role_mark_inventory_dirty("warehouse-deposit");
    printf("[info][network] mock_warehouse_deposit role=%u bag_seq=%u item=%u "
           "enhance=%u warehouse_slots=%u bag_rows=%u\n",
           role->roleId, backpackSeq, moved.itemId, moved.enhanceLevel,
           warehouse->itemCount, role->backpackItemCount);
    return true;
}

static bool vm_net_mock_warehouse_retrieve_slot(vm_net_mock_role_state *role,
                                                u32 slotIndex,
                                                u16 *backpackSeqOut,
                                                const char **reasonOut)
{
    vm_net_mock_warehouse_state *warehouse = NULL;
    vm_net_mock_backpack_item_state moved;
    vm_net_mock_role_state beforeRole;
    vm_net_mock_warehouse_state beforeWarehouse;
    u16 addedSeq = 0;
    u32 i;

    if (backpackSeqOut)
        *backpackSeqOut = 0;
    if (reasonOut)
        *reasonOut = "ok";
    if (role == NULL)
    {
        if (reasonOut)
            *reasonOut = "bad-args";
        return false;
    }
    warehouse = vm_net_mock_warehouse_active();
    if (warehouse == NULL || slotIndex >= warehouse->itemCount)
    {
        if (reasonOut)
            *reasonOut = "slot-missing";
        return false;
    }

    beforeRole = *role;
    beforeWarehouse = *warehouse;
    moved = warehouse->items[slotIndex];
    for (i = slotIndex + 1; i < warehouse->itemCount; ++i)
        warehouse->items[i - 1] = warehouse->items[i];
    if (warehouse->itemCount > 0)
        --warehouse->itemCount;
    memset(&warehouse->items[warehouse->itemCount], 0,
           sizeof(warehouse->items[0]));

    if (vm_net_mock_backpack_item_id_uses_reservoir_count(moved.itemId))
    {
        if (!vm_net_mock_role_grant_flask_reservoir_points(
                role, moved.itemId, moved.count, &addedSeq,
                "warehouse-retrieve-flask"))
        {
            *warehouse = beforeWarehouse;
            if (reasonOut)
            {
                *reasonOut =
                    g_vm_net_mock_backpack_add_fail_kind ==
                            VM_NET_MOCK_BACKPACK_ADD_FAIL_FLASK_FULL
                        ? "flask-reservoir-full"
                        : "backpack-full";
            }
            return false;
        }
    }
    else if (vm_net_mock_backpack_item_id_is_durable_mall_pass(moved.itemId))
    {
        if (!vm_net_mock_role_grant_durable_pass_remaining(
                role, moved.itemId, moved.count, &addedSeq,
                "warehouse-retrieve-durable-pass"))
        {
            *warehouse = beforeWarehouse;
            if (reasonOut)
                *reasonOut = "backpack-full";
            return false;
        }
    }
    else
    {
        u32 addCount = moved.count;
        if (vm_net_mock_find_equipment_catalog_item(moved.itemId) != NULL)
            addCount = 1;
        if (!vm_net_mock_role_add_backpack_item_to_role(
                role, moved.itemId, addCount, moved.enhanceLevel, &addedSeq,
                "warehouse-retrieve-bag", &moved.enhanceWash,
                moved.enhanceInstanceId))
        {
            *warehouse = beforeWarehouse;
            if (reasonOut)
                *reasonOut = "backpack-full";
            return false;
        }
    }
    /* add_backpack already marked dirty; re-mark after reservoir/enhance fix. */
    (void)beforeRole;
    (void)beforeWarehouse;
    vm_net_mock_role_mark_inventory_dirty("warehouse-retrieve");
    if (backpackSeqOut)
        *backpackSeqOut = addedSeq;
    return true;
}

static vm_net_mock_backpack_item_state *vm_net_mock_role_find_backpack_item(
    vm_net_mock_role_state *role, u32 itemId, u16 seq);

/*
 * One backpack row; item.count = remaining open-uses (wire durability).
 * Does not multiply into N full mall vouchers.
 */
static bool vm_net_mock_role_grant_durable_pass_remaining(
    vm_net_mock_role_state *role,
    u32 itemId,
    u32 remaining,
    u16 *seqOut,
    const char *reason)
{
    u32 itemCount = 0;
    u32 maxDur = 0;
    u32 freeSlots = 0;
    vm_net_mock_backpack_item_state *item = NULL;

    if (seqOut)
        *seqOut = 0;
    g_vm_net_mock_backpack_add_fail_kind = VM_NET_MOCK_BACKPACK_ADD_FAIL_NONE;
    if (role == NULL || remaining == 0)
        return false;
    if (itemId == VM_NET_MOCK_WAREHOUSE_PASS_ITEM_ID)
        maxDur = VM_NET_MOCK_WAREHOUSE_PASS_DURABILITY;
    else if (itemId == VM_NET_MOCK_EQUIP_SELL_PASS_ITEM_ID)
        maxDur = VM_NET_MOCK_EQUIP_SELL_PASS_DURABILITY;
    else
        return false;
    if (remaining > maxDur)
        remaining = maxDur;

    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    freeSlots = role->backpackCapacity > itemCount
                    ? (u32)role->backpackCapacity - itemCount
                    : 0;
    if (freeSlots < 1 || itemCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
    {
        g_vm_net_mock_backpack_add_fail_kind = VM_NET_MOCK_BACKPACK_ADD_FAIL_SPACE;
        return false;
    }

    item = &role->backpackItems[itemCount];
    memset(item, 0, sizeof(*item));
    item->itemId = itemId;
    item->seq = role->nextBackpackSeq ? role->nextBackpackSeq : 1;
    item->count = remaining;
    role->nextBackpackSeq = (u16)(item->seq + 1);
    if (role->nextBackpackSeq == 0)
        role->nextBackpackSeq = 1;
    role->backpackItemCount = (u16)(itemCount + 1);
    if (seqOut)
        *seqOut = item->seq;
    vm_net_mock_role_normalize_backpack(role);
    vm_net_mock_role_mark_inventory_dirty(
        reason ? reason : "backpack-grant-durable-pass");
    printf("[info][network] mock_grant_durable_pass role=%u item=%u remaining=%u "
           "seq=%u reason=%s evidence=one-row-not-mall-unit-loop\n",
           role->roleId, itemId, remaining, (u32)item->seq,
           reason ? reason : "-");
    return true;
}

/*
 * Grant HP/MP reservoir points onto a single bag row for itemId (802/803).
 * Merges duplicate rows first.  Rejects when the add would exceed MAX.
 */
static bool vm_net_mock_role_grant_flask_reservoir_points(
    vm_net_mock_role_state *role,
    u32 itemId,
    u32 grantPoints,
    u16 *seqOut,
    const char *reason)
{
    u32 itemCount = 0;
    vm_net_mock_backpack_item_state *existing = NULL;
    u16 keepSeq = 0;
    u32 beforePoints = 0;

    if (seqOut)
        *seqOut = 0;
    g_vm_net_mock_backpack_add_fail_kind = VM_NET_MOCK_BACKPACK_ADD_FAIL_NONE;
    if (role == NULL || itemId == 0 || grantPoints == 0 ||
        !vm_net_mock_backpack_item_id_uses_reservoir_count(itemId))
    {
        g_vm_net_mock_backpack_add_fail_kind = VM_NET_MOCK_BACKPACK_ADD_FAIL_SPACE;
        return false;
    }

    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    for (u32 i = 0; i < itemCount; ++i)
    {
        vm_net_mock_backpack_item_state *row = &role->backpackItems[i];
        if (row->itemId != itemId || row->count == 0)
            continue;
        if (existing == NULL)
        {
            existing = row;
            keepSeq = row->seq;
        }
        else
        {
            if (existing->count < VM_NET_MOCK_FLASK_MAX_RESERVOIR)
            {
                u32 room = VM_NET_MOCK_FLASK_MAX_RESERVOIR - existing->count;
                existing->count += row->count > room ? room : row->count;
            }
            row->count = 0;
        }
    }
    if (existing != NULL)
    {
        vm_net_mock_role_normalize_backpack(role);
        existing = vm_net_mock_role_find_backpack_item(role, itemId, keepSeq);
        if (existing == NULL)
            existing = vm_net_mock_role_find_backpack_item(role, itemId, 0);
    }

    if (existing != NULL)
    {
        beforePoints = existing->count;
        if (beforePoints >= VM_NET_MOCK_FLASK_MAX_RESERVOIR ||
            grantPoints > VM_NET_MOCK_FLASK_MAX_RESERVOIR - beforePoints)
        {
            g_vm_net_mock_backpack_add_fail_kind =
                VM_NET_MOCK_BACKPACK_ADD_FAIL_FLASK_FULL;
            return false;
        }
        existing->count = beforePoints + grantPoints;
        if (seqOut)
            *seqOut = existing->seq != 0 ? existing->seq : keepSeq;
        vm_net_mock_role_normalize_backpack(role);
        vm_net_mock_role_mark_inventory_dirty(
            reason ? reason : "backpack-merge-reservoir-item");
        return true;
    }

    if (grantPoints > VM_NET_MOCK_FLASK_MAX_RESERVOIR)
    {
        g_vm_net_mock_backpack_add_fail_kind =
            VM_NET_MOCK_BACKPACK_ADD_FAIL_FLASK_FULL;
        return false;
    }
    itemCount = vm_net_mock_role_backpack_count(role);
    if (itemCount >= role->backpackCapacity ||
        itemCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
    {
        g_vm_net_mock_backpack_add_fail_kind = VM_NET_MOCK_BACKPACK_ADD_FAIL_SPACE;
        return false;
    }
    {
        vm_net_mock_backpack_item_state *item = &role->backpackItems[itemCount];
        memset(item, 0, sizeof(*item));
        item->itemId = itemId;
        item->seq = role->nextBackpackSeq;
        if (item->seq == 0)
            item->seq = 1;
        item->count = grantPoints;
        if (seqOut)
            *seqOut = item->seq;
        role->nextBackpackSeq = (u16)(item->seq + 1);
        if (role->nextBackpackSeq == 0)
            role->nextBackpackSeq = 1;
        role->backpackItemCount = (u16)(itemCount + 1);
    }
    vm_net_mock_role_normalize_backpack(role);
    vm_net_mock_role_mark_inventory_dirty(
        reason ? reason : "backpack-add-reservoir-item");
    return true;
}

static u32 vm_net_mock_role_alloc_enhance_instance_id(vm_net_mock_role_state *role)
{
    u32 id = 0;

    if (role == NULL)
        return 0;
    id = role->nextEnhanceInstanceId;
    if (id == 0)
        id = 1;
    role->nextEnhanceInstanceId = id + 1;
    if (role->nextEnhanceInstanceId == 0)
        role->nextEnhanceInstanceId = 1;
    return id;
}

static u32 vm_net_mock_role_ensure_equipment_instance_id(vm_net_mock_role_state *role,
                                                           u32 existingId,
                                                           u32 itemId)
{
    if (role == NULL || vm_net_mock_find_equipment_catalog_item(itemId) == NULL)
        return 0;
    if (existingId != 0)
        return existingId;
    return vm_net_mock_role_alloc_enhance_instance_id(role);
}

static void vm_net_mock_role_refresh_enhance_instance_counter(
    vm_net_mock_role_state *role)
{
    u32 maxId = 0;
    u32 i = 0;

    if (role == NULL)
        return;
    for (i = 0; i < VM_NET_MOCK_BACKPACK_MAX_ITEMS; ++i)
    {
        if (role->backpackItems[i].enhanceInstanceId > maxId)
            maxId = role->backpackItems[i].enhanceInstanceId;
    }
    for (i = 0; i < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++i)
    {
        if (role->equippedEnhanceInstanceIds[i] > maxId)
            maxId = role->equippedEnhanceInstanceIds[i];
    }
    role->nextEnhanceInstanceId = maxId + 1;
    if (role->nextEnhanceInstanceId == 0)
        role->nextEnhanceInstanceId = 1;
}

static void vm_net_mock_role_finalize_loaded_enhance_instances(
    vm_net_mock_role_state *role)
{
    u32 i = 0;
    u32 itemCount = 0;

    if (role == NULL)
        return;
    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    for (i = 0; i < itemCount && i < VM_NET_MOCK_BACKPACK_MAX_ITEMS; ++i)
    {
        vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        if (item->itemId == 0 || item->count == 0)
            continue;
        item->enhanceInstanceId = vm_net_mock_role_ensure_equipment_instance_id(
            role, item->enhanceInstanceId, item->itemId);
    }
    for (i = 0; i < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++i)
    {
        if (role->equippedItemIds[i] == 0)
            continue;
        role->equippedEnhanceInstanceIds[i] =
            vm_net_mock_role_ensure_equipment_instance_id(
                role, role->equippedEnhanceInstanceIds[i],
                role->equippedItemIds[i]);
    }
    vm_net_mock_role_refresh_enhance_instance_counter(role);
}

static bool vm_net_mock_role_add_backpack_item_to_role(vm_net_mock_role_state *role,
                                                        u32 itemId,
                                                        u32 count,
                                                        u16 enhanceLevel,
                                                        u16 *seqOut,
                                                        const char *reason,
                                                        const vm_net_mock_enhance_wash_state *wash,
                                                        u32 enhanceInstanceId)
{
    const vm_net_mock_item_effect_catalog_item *effect = NULL;
    u32 reservoirCapacity = 0;
    u32 itemCount = 0;
    vm_net_mock_role_state before;
    u16 clampedEnhance = enhanceLevel;

    if (seqOut)
        *seqOut = 0;
    g_vm_net_mock_backpack_add_fail_kind = VM_NET_MOCK_BACKPACK_ADD_FAIL_NONE;
    if (role == NULL || itemId == 0 || count == 0)
        return false;
    if (clampedEnhance > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
        clampedEnhance = VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL;

    before = *role;
    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    effect = vm_net_mock_find_item_effect_catalog_item(itemId);
    reservoirCapacity = vm_net_mock_item_effect_reservoir_capacity(effect);
    if (reservoirCapacity == 0 &&
        vm_net_mock_backpack_item_id_uses_reservoir_count(itemId))
    {
        reservoirCapacity = VM_NET_MOCK_FLASK_UNIT_RESERVOIR;
    }

    /* Mall warehouse pass: one backpack row, count = remaining durability. */
    if (itemId == VM_NET_MOCK_WAREHOUSE_PASS_ITEM_ID ||
        itemId == VM_NET_MOCK_EQUIP_SELL_PASS_ITEM_ID ||
        itemId == VM_NET_MOCK_SPAR_SUBSTITUTE_DOLL_ITEM_ID ||
        itemId == VM_NET_MOCK_HUNT_ORDER_ITEM_ID)
    {
        u32 freeSlots = role->backpackCapacity > itemCount
                            ? (u32)role->backpackCapacity - itemCount
                            : 0;
        u16 firstSeq = 0;
        u32 passDurability =
            itemId == VM_NET_MOCK_EQUIP_SELL_PASS_ITEM_ID
                ? VM_NET_MOCK_EQUIP_SELL_PASS_DURABILITY
                : (itemId == VM_NET_MOCK_SPAR_SUBSTITUTE_DOLL_ITEM_ID ||
                           itemId == VM_NET_MOCK_HUNT_ORDER_ITEM_ID
                       ? VM_NET_MOCK_HUNT_ORDER_DURABILITY
                       : VM_NET_MOCK_WAREHOUSE_PASS_DURABILITY);
        const char *saveReason =
            itemId == VM_NET_MOCK_EQUIP_SELL_PASS_ITEM_ID
                ? "backpack-add-equip-sell-pass"
                : (itemId == VM_NET_MOCK_SPAR_SUBSTITUTE_DOLL_ITEM_ID
                       ? "backpack-add-spar-substitute-doll"
                       : (itemId == VM_NET_MOCK_HUNT_ORDER_ITEM_ID
                              ? "backpack-add-hunt-order"
                              : "backpack-add-warehouse-pass"));

        /* 替身人偶 / 追杀令：耐久1，叠加入已有堆或新行（count=剩余次数）。 */
        if (itemId == VM_NET_MOCK_SPAR_SUBSTITUTE_DOLL_ITEM_ID ||
            itemId == VM_NET_MOCK_HUNT_ORDER_ITEM_ID)
        {
            vm_net_mock_backpack_item_state *existing =
                vm_net_mock_role_find_backpack_item(role, itemId, 0);
            u32 grant = count * passDurability;

            if (grant == 0)
                return false;
            if (existing != NULL)
            {
                existing->count += grant;
                if (seqOut)
                    *seqOut = existing->seq;
                vm_net_mock_role_normalize_backpack(role);
                (void)before;
                vm_net_mock_role_mark_inventory_dirty(saveReason);
                return true;
            }
            if (freeSlots < 1 || itemCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
                return false;
            {
                vm_net_mock_backpack_item_state *item =
                    &role->backpackItems[itemCount];
                memset(item, 0, sizeof(*item));
                item->itemId = itemId;
                item->seq = role->nextBackpackSeq ? role->nextBackpackSeq : 1;
                item->count = grant;
                firstSeq = item->seq;
                role->nextBackpackSeq = (u16)(item->seq + 1);
                if (role->nextBackpackSeq == 0)
                    role->nextBackpackSeq = 1;
                role->backpackItemCount = (u16)(itemCount + 1);
            }
            if (seqOut)
                *seqOut = firstSeq;
            vm_net_mock_role_normalize_backpack(role);
            (void)before;
            vm_net_mock_role_mark_inventory_dirty(saveReason);
            return true;
        }

        if (count > freeSlots || count > VM_NET_MOCK_BACKPACK_MAX_ITEMS - itemCount)
            return false;
        for (u32 unit = 0; unit < count; ++unit)
        {
            vm_net_mock_backpack_item_state *item =
                &role->backpackItems[itemCount + unit];
            memset(item, 0, sizeof(*item));
            item->itemId = itemId;
            item->seq = role->nextBackpackSeq ? role->nextBackpackSeq : 1;
            item->count = passDurability;
            if (firstSeq == 0)
                firstSeq = item->seq;
            role->nextBackpackSeq = (u16)(item->seq + 1);
            if (role->nextBackpackSeq == 0)
                role->nextBackpackSeq = 1;
        }
        role->backpackItemCount = (u16)(itemCount + count);
        if (seqOut)
            *seqOut = firstSeq;
        vm_net_mock_role_normalize_backpack(role);
        (void)before;
        vm_net_mock_role_mark_inventory_dirty(reason ? reason : saveReason);
        return true;
    }

    if (reservoirCapacity != 0 ||
        vm_net_mock_backpack_item_id_uses_reservoir_count(itemId))
    {
        u32 grantPoints = 0;

        g_vm_net_mock_backpack_add_fail_kind = VM_NET_MOCK_BACKPACK_ADD_FAIL_NONE;
        if (reservoirCapacity == 0)
            reservoirCapacity = VM_NET_MOCK_FLASK_UNIT_RESERVOIR;
        if (count > 0xffffffffu / reservoirCapacity)
        {
            g_vm_net_mock_backpack_add_fail_kind =
                VM_NET_MOCK_BACKPACK_ADD_FAIL_FLASK_FULL;
            return false;
        }
        grantPoints = count * reservoirCapacity;
        (void)before;
        return vm_net_mock_role_grant_flask_reservoir_points(
            role, itemId, grantPoints, seqOut,
            reason ? reason : "backpack-add-reservoir-item");
    }

    /*
     * Equipment instances are never merged by itemId.  Stacking them made
     * trade/drop look like multi-copy grants, made discard wipe every same-name
     * piece, and zeroed enhance on the surviving row.
     */
    if (vm_net_mock_find_equipment_catalog_item(itemId) != NULL)
    {
        u32 freeSlots = role->backpackCapacity > itemCount
                            ? (u32)role->backpackCapacity - itemCount
                            : 0;
        u16 firstSeq = 0;

        if (count > freeSlots || count > VM_NET_MOCK_BACKPACK_MAX_ITEMS - itemCount)
            return false;
        for (u32 unit = 0; unit < count; ++unit)
        {
            vm_net_mock_backpack_item_state *item = &role->backpackItems[itemCount + unit];
            memset(item, 0, sizeof(*item));
            item->itemId = itemId;
            item->seq = role->nextBackpackSeq ? role->nextBackpackSeq : 1;
            item->count = 1;
            item->enhanceLevel = clampedEnhance;
            item->enhanceInstanceId = vm_net_mock_role_ensure_equipment_instance_id(
                role, enhanceInstanceId, itemId);
            if (wash != NULL)
                vm_net_mock_enhance_wash_copy(&item->enhanceWash, wash);
            if (firstSeq == 0)
                firstSeq = item->seq;
            role->nextBackpackSeq = (u16)(item->seq + 1);
            if (role->nextBackpackSeq == 0)
                role->nextBackpackSeq = 1;
        }
        role->backpackItemCount = (u16)(itemCount + count);
        if (seqOut)
            *seqOut = firstSeq;
        vm_net_mock_role_normalize_backpack(role);
        (void)before;
        vm_net_mock_role_mark_inventory_dirty(
            reason ? reason : "backpack-add-equipment");
        return true;
    }

    for (u32 i = 0; i < itemCount; ++i)
    {
        vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        if (item->itemId == itemId)
        {
            if (0xffffffffu - item->count < count)
                item->count = 0xffffffffu;
            else
                item->count += count;
            if (seqOut)
                *seqOut = item->seq;
            vm_net_mock_role_normalize_backpack(role);
            (void)before;
            vm_net_mock_role_mark_inventory_dirty(
                reason ? reason : "backpack-add-stack");
            return true;
        }
    }

    if (itemCount >= role->backpackCapacity || itemCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        return false;

    vm_net_mock_backpack_item_state *item = &role->backpackItems[itemCount];
    memset(item, 0, sizeof(*item));
    item->itemId = itemId;
    item->seq = role->nextBackpackSeq;
    if (item->seq == 0)
        item->seq = 1;
    item->count = count;
    role->backpackItemCount = (u16)(itemCount + 1);
    role->nextBackpackSeq = (u16)(item->seq + 1);
    if (role->nextBackpackSeq == 0)
        role->nextBackpackSeq = 1;
    if (seqOut)
        *seqOut = item->seq;
    vm_net_mock_role_normalize_backpack(role);
    (void)before;
    vm_net_mock_role_mark_inventory_dirty(reason ? reason : "backpack-add-item");
    return true;
}

static bool vm_net_mock_role_add_backpack_item(u32 itemId, u32 count, u16 *seqOut)
{
    return vm_net_mock_role_add_backpack_item_to_role(
        vm_net_mock_active_role(), itemId, count, 0, seqOut, NULL, NULL, 0);
}

static bool vm_net_mock_role_add_backpack_item_enhanced(u32 itemId, u32 count,
                                                        u16 enhanceLevel,
                                                        u16 *seqOut,
                                                        const vm_net_mock_enhance_wash_state *wash)
{
    return vm_net_mock_role_add_backpack_item_to_role(
        vm_net_mock_active_role(), itemId, count, enhanceLevel, seqOut, NULL,
        wash, 0);
}

static vm_net_mock_backpack_item_state *vm_net_mock_role_find_backpack_item(vm_net_mock_role_state *role,
                                                                            u32 itemId,
                                                                            u16 seq)
{
    u32 itemCount = 0;

    if (role == NULL || (itemId == 0 && seq == 0))
        return NULL;

    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    if (seq != 0)
    {
        for (u32 i = 0; i < itemCount; ++i)
        {
            vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
            if (item->seq == seq && (itemId == 0 || item->itemId == itemId))
                return item;
        }
    }
    if (itemId != 0)
    {
        for (u32 i = 0; i < itemCount; ++i)
        {
            vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
            if (item->itemId == itemId)
                return item;
        }
    }
    return NULL;
}

static vm_net_mock_backpack_item_state *vm_net_mock_role_find_backpack_item_by_effect(vm_net_mock_role_state *role,
                                                                                      u32 hp,
                                                                                      u32 mp,
                                                                                      u32 exp)
{
    u32 itemCount = 0;

    if (role == NULL || (hp == 0 && mp == 0 && exp == 0))
        return NULL;
    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    for (u32 i = 0; i < itemCount; ++i)
    {
        vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        const vm_net_mock_item_effect_catalog_item *effect =
            vm_net_mock_find_item_effect_catalog_item(item->itemId);
        if (!vm_net_mock_item_effect_is_usable(effect))
            continue;
        if ((hp == 0 || effect->hp == hp) &&
            (mp == 0 || effect->mp == mp) &&
            (exp == 0 || effect->exp == exp))
        {
            return item;
        }
    }
    for (u32 i = 0; i < itemCount; ++i)
    {
        vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        const vm_net_mock_item_effect_catalog_item *effect =
            vm_net_mock_find_item_effect_catalog_item(item->itemId);
        if (vm_net_mock_item_effect_is_usable(effect))
            return item;
    }
    return NULL;
}

static bool vm_net_mock_role_consume_backpack_item(vm_net_mock_role_state *role,
                                                   u32 itemId,
                                                   u16 seq,
                                                   u32 count,
                                                   u32 *remainingOut)
{
    vm_net_mock_backpack_item_state *item = NULL;

    if (remainingOut)
        *remainingOut = 0;
    if (role == NULL || count == 0)
        return false;

    item = vm_net_mock_role_find_backpack_item(role, itemId, seq);
    if (item == NULL || item->count == 0)
        return false;

    if (item->count <= count)
    {
        item->count = 0;
        if (remainingOut)
            *remainingOut = 0;
    }
    else
    {
        item->count -= count;
        if (remainingOut)
            *remainingOut = item->count;
    }
    vm_net_mock_role_normalize_backpack(role);
    return true;
}

/* Drain `amount` across stacks (seq=0 finds next non-empty).  Each modified
 * row is recorded for client 7/7 type=2 + 7/11 remaining sync. */
static bool vm_net_mock_role_consume_backpack_item_amount(
    vm_net_mock_role_state *role, u32 itemId, u32 amount,
    vm_net_mock_consume_sync_row *syncRows, u32 syncCap, u32 *syncCountOut)
{
    u32 need = amount;
    u32 syncCount = 0;
    u32 held = 0;
    u32 itemCount = 0;

    if (syncCountOut)
        *syncCountOut = 0;
    if (role == NULL || itemId == 0 || amount == 0)
        return false;
    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    for (u32 i = 0; i < itemCount; ++i)
    {
        if (role->backpackItems[i].itemId == itemId)
            held += role->backpackItems[i].count;
    }
    if (held < amount)
        return false;

    while (need > 0)
    {
        vm_net_mock_backpack_item_state *item =
            vm_net_mock_role_find_backpack_item(role, itemId, 0);
        u32 take = 0;
        u32 remaining = 0;
        u16 seq = 0;

        if (item == NULL || item->count == 0)
            return false;
        seq = item->seq;
        take = item->count < need ? item->count : need;
        if (!vm_net_mock_role_consume_backpack_item(role, itemId, seq, take,
                                                     &remaining))
        {
            return false;
        }
        if (syncRows != NULL && syncCount < syncCap)
        {
            syncRows[syncCount].seq = seq;
            syncRows[syncCount].itemId = itemId;
            syncRows[syncCount].remaining = remaining;
            ++syncCount;
        }
        need -= take;
    }
    if (syncCountOut)
        *syncCountOut = syncCount;
    return true;
}

/* Purge dream material 841 from backpack memory/DB dirty.  Callers that
 * own a live session should also arm backpackListResync for 17/1. */
static u32 vm_net_mock_dream_purge_item_841(vm_net_mock_role_state *role,
                                            const char *reason)
{
    u32 removed = 0;
    u32 itemCount = 0;

    if (role == NULL)
        return 0;
    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    for (u32 i = 0; i < itemCount; ++i)
    {
        if (role->backpackItems[i].itemId == 841u &&
            role->backpackItems[i].count != 0)
        {
            removed += role->backpackItems[i].count;
            role->backpackItems[i].count = 0;
        }
    }
    if (removed == 0)
        return 0;
    vm_net_mock_role_normalize_backpack(role);
    vm_net_mock_role_mark_inventory_dirty(reason ? reason : "dream-leave-purge-841");
    printf("[info][network] mock_dream_purge_841 role=%u removed=%u reason=%s\n",
           role->roleId, removed, reason ? reason : "-");
    return removed;
}

static bool vm_net_mock_role_equip_backpack_item(vm_net_mock_role_state *role,
                                                 u32 requestedItemId,
                                                 u16 requestedSeq,
                                                 u32 *equippedItemIdOut,
                                                 u16 *equippedSeqOut,
                                                 u8 *slotOut,
                                                 u32 *oldItemIdOut,
                                                 const char **reasonOut)
{
    vm_net_mock_backpack_item_state *item = NULL;
    const vm_net_mock_equipment_catalog_item *equip = NULL;
    u32 itemId = 0;
    u16 seq = 0;
    u32 oldItemId = 0;
    u8 slot = 0xff;
    u32 remaining = 0;
    bool selectedFreesSlot = false;

    if (equippedItemIdOut)
        *equippedItemIdOut = 0;
    if (equippedSeqOut)
        *equippedSeqOut = 0;
    if (slotOut)
        *slotOut = 0xff;
    if (oldItemIdOut)
        *oldItemIdOut = 0;
    if (reasonOut)
        *reasonOut = "ok";

    if (role == NULL)
    {
        if (reasonOut)
            *reasonOut = "no-role";
        return false;
    }

    item = vm_net_mock_role_find_backpack_item(role, requestedItemId, requestedSeq);
    if (item == NULL && requestedSeq != 0)
        item = vm_net_mock_role_find_backpack_item(role, 0, requestedSeq);
    if (item == NULL && requestedItemId != 0)
        item = vm_net_mock_role_find_backpack_item(role, requestedItemId, 0);
    if (item == NULL)
    {
        if (reasonOut)
            *reasonOut = "item-not-found";
        return false;
    }

    itemId = item->itemId;
    seq = item->seq;
    selectedFreesSlot = item->count <= 1;
    equip = vm_net_mock_find_equipment_catalog_item(itemId);
    if (equip == NULL || equip->slot >= VM_NET_MOCK_EQUIP_SLOT_COUNT)
    {
        if (reasonOut)
            *reasonOut = "not-equipment";
        return false;
    }
    if (role->level == 0)
        role->level = vm_net_mock_role_level_from_exp(role->exp);
    if (role->level < equip->levelRequired)
    {
        if (reasonOut)
            *reasonOut = "level-too-low";
        return false;
    }
    if (!vm_net_mock_role_job_allows_weapon_category(role->job, equip->category))
    {
        if (reasonOut)
            *reasonOut = "wrong-job-weapon";
        return false;
    }

    slot = equip->slot;
    oldItemId = role->equippedItemIds[slot];
    if (oldItemId != 0)
    {
        bool oldAlreadyStacked = vm_net_mock_role_find_backpack_item(role, oldItemId, 0) != NULL;
        u32 itemCount = vm_net_mock_role_backpack_count(role);
        if (!oldAlreadyStacked && !selectedFreesSlot && itemCount >= role->backpackCapacity)
        {
            if (reasonOut)
                *reasonOut = "bag-full-for-old";
            return false;
        }
    }

    {
        u16 newEnhance = item->enhanceLevel;
        u16 oldEnhance = role->equippedEnhanceLevels[slot];
        u32 newInstanceId = item->enhanceInstanceId;
        u32 oldInstanceId = role->equippedEnhanceInstanceIds[slot];
        vm_net_mock_enhance_wash_state newWash;
        vm_net_mock_enhance_wash_state oldWash;

        vm_net_mock_enhance_wash_copy(&newWash, &item->enhanceWash);
        vm_net_mock_enhance_wash_copy(&oldWash, &role->equippedEnhanceWash[slot]);
        newInstanceId = vm_net_mock_role_ensure_equipment_instance_id(
            role, newInstanceId, itemId);
        vm_net_mock_enhance_wash_coalesce_from_record(
            role->roleId, newInstanceId, itemId, item->enhanceLevel, seq,
            &newWash);
        vm_net_mock_enhance_wash_coalesce_from_record(
            role->roleId, oldInstanceId, role->equippedItemIds[slot],
            role->equippedEnhanceLevels[slot], 0, &oldWash);
        if (newEnhance > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
            newEnhance = VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL;
        if (oldEnhance > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
            oldEnhance = VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL;

        if (!vm_net_mock_role_consume_backpack_item(role, itemId, seq, 1, &remaining))
        {
            if (reasonOut)
                *reasonOut = "consume-failed";
            return false;
        }
        role->equippedItemIds[slot] = itemId;
        role->equippedEnhanceLevels[slot] = newEnhance;
        role->equippedEnhanceInstanceIds[slot] = newInstanceId;
        vm_net_mock_enhance_wash_copy(&role->equippedEnhanceWash[slot], &newWash);
        if (oldItemId != 0)
        {
            u16 oldSeq = 0;
            if (!vm_net_mock_role_add_backpack_item_to_role(
                    role, oldItemId, 1, oldEnhance, &oldSeq, NULL, &oldWash,
                    oldInstanceId))
            {
                role->equippedItemIds[slot] = oldItemId;
                role->equippedEnhanceLevels[slot] = oldEnhance;
                role->equippedEnhanceInstanceIds[slot] = oldInstanceId;
                vm_net_mock_enhance_wash_copy(&role->equippedEnhanceWash[slot],
                                              &oldWash);
                (void)vm_net_mock_role_add_backpack_item_to_role(
                    role, itemId, 1, newEnhance, NULL, NULL, &newWash,
                    newInstanceId);
                if (reasonOut)
                    *reasonOut = "old-return-failed";
                return false;
            }
        }
    }

    vm_net_mock_role_sync_derived_vitals(role);
    vm_net_mock_role_mark_inventory_dirty("item-equip");
    (void)vm_net_mock_role_refresh_special_designations(role);

    if (equippedItemIdOut)
        *equippedItemIdOut = itemId;
    /*
     * HandleItemOperationResponse(7/8 type=3) writes response seq into
     * item+276.  Unequip/swap requests only carry that seq (no itemId) —
     * BuildGameEventPacket subtype 8 emits {type,seq}.  Login equipment
     * rows and 7/9 body use seq=slot+1; returning the old backpack seq
     * makes multi-slot unequip hit ambiguous-seq / not-found.
     */
    if (equippedSeqOut)
        *equippedSeqOut = (u16)(slot + 1u);
    if (slotOut)
        *slotOut = slot;
    if (oldItemIdOut)
        *oldItemIdOut = oldItemId;
    if (reasonOut)
        *reasonOut = "ok";
    (void)seq;
    (void)remaining;
    return true;
}

u32 vm_net_mock_copy_outdoor_field_scenes(char scenes[][64], u32 cap)
{
    u32 total = vm_net_mock_load_auto_monster_catalog();
    u32 count = 0;

    if (scenes == NULL || cap == 0)
        return 0;
    for (u32 i = 0; i < total && count < cap; ++i)
    {
        const vm_net_mock_auto_monster_catalog_item *item =
            &g_vm_net_mock_auto_monster_catalog[i];
        if (item->scene[0] == 0 ||
            vm_net_mock_scene_is_city_hangup_forbidden(item->scene))
            continue;
        snprintf(scenes[count], 64, "%s", item->scene);
        ++count;
    }
    return count;
}

bool vm_net_mock_pick_scene_spawn_xy(const char *scene, u16 *xOut, u16 *yOut)
{
    u16 x = 120;
    u16 y = 120;
    u16 entryId = 0;

    if (scene == NULL || scene[0] == 0)
        return false;
    if (vm_net_mock_get_scene_reasonable_spawn_from_sce(scene, &x, &y, &entryId))
        vm_net_mock_adjust_safe_player_pos_for_scene(scene, &x, &y);
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
