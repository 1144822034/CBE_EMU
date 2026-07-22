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

static u32 vm_net_mock_load_auto_monster_catalog(void)
{
    u32 added = 0;

    if (g_vm_net_mock_auto_monster_catalog_loaded)
        return g_vm_net_mock_auto_monster_catalog_count;

    g_vm_net_mock_auto_monster_catalog_loaded = true;
    g_vm_net_mock_auto_monster_catalog_count = 0;
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
    return g_vm_net_mock_auto_monster_catalog_count;
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

typedef struct
{
    vm_mock_service_account_db_file *database;
    bool invalid;
} vm_mock_mysql_account_load_context;

static bool vm_mock_mysql_account_row(void *context_value,
                                      unsigned int column_count,
                                      const char *const *values,
                                      const size_t *lengths)
{
    vm_mock_mysql_account_load_context *context = (vm_mock_mysql_account_load_context *)context_value;
    if (context == NULL || context->database == NULL || column_count != 2 ||
        context->database->accountCount >= VM_MOCK_SERVICE_ACCOUNT_DB_MAX_ACCOUNTS)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    vm_mock_service_account_record *record =
        &context->database->accounts[context->database->accountCount];
    size_t password_len = 0;
    memset(record, 0, sizeof(*record));
    if (!vm_mock_mysql_copy_text(record->username, sizeof(record->username), values[0], lengths[0]) ||
        values[1] == NULL ||
        !vm_mysql_hex_decode(values[1], lengths[1], record->password,
                             sizeof(record->password) - 1, &password_len))
    {
        context->invalid = true;
        memset(record, 0, sizeof(*record));
        return true;
    }
    record->password[password_len] = 0;
    ++context->database->accountCount;
    return true;
}

static bool vm_mock_service_account_db_load_legacy(void)
{
    char path[128];
    vm_mock_service_account_db_file loaded;
    vm_mock_service_account_db_path(path, sizeof(path));
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return false;
    memset(&loaded, 0, sizeof(loaded));
    bool valid = fread(&loaded, 1, sizeof(loaded), fp) == sizeof(loaded) &&
                 memcmp(loaded.magic, "JHA1", 4) == 0 &&
                 loaded.version == VM_MOCK_SERVICE_ACCOUNT_DB_VERSION &&
                 loaded.accountCount <= VM_MOCK_SERVICE_ACCOUNT_DB_MAX_ACCOUNTS;
    fclose(fp);
    if (!valid)
        return false;
    g_vm_mock_service_account_db = loaded;
    return true;
}

static void vm_mock_service_account_db_save(const char *reason)
{
    if (!g_vm_mock_service_account_db_valid)
        return;
    memcpy(g_vm_mock_service_account_db.magic, "JHA1", 4);
    g_vm_mock_service_account_db.version = VM_MOCK_SERVICE_ACCOUNT_DB_VERSION;
    if (g_vm_mock_service_account_db.accountCount > VM_MOCK_SERVICE_ACCOUNT_DB_MAX_ACCOUNTS)
        g_vm_mock_service_account_db.accountCount = VM_MOCK_SERVICE_ACCOUNT_DB_MAX_ACCOUNTS;

    if (!vm_mysql_exec("START TRANSACTION") || !vm_mysql_exec("DELETE FROM accounts"))
    {
        char mysql_error[512];
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        vm_mysql_exec("ROLLBACK");
        vm_autotest_note("mock_account_db_mysql_save_failed reason=%s error=%s\n",
                         reason ? reason : "state", mysql_error);
        return;
    }
    for (u32 i = 0; i < g_vm_mock_service_account_db.accountCount; ++i)
    {
        const vm_mock_service_account_record *record = &g_vm_mock_service_account_db.accounts[i];
        char username_hex[sizeof(record->username) * 2 + 1];
        char password_hex[sizeof(record->password) * 2 + 1];
        char query[768];
        size_t username_len = vm_mock_mysql_bounded_strlen(record->username, sizeof(record->username));
        size_t password_len = vm_mock_mysql_bounded_strlen(record->password, sizeof(record->password));
        if (username_len == 0 || username_len >= sizeof(record->username) ||
            password_len >= sizeof(record->password) ||
            vm_mysql_hex_encode(record->username, username_len, username_hex, sizeof(username_hex)) == 0 ||
            vm_mysql_hex_encode(record->password, password_len, password_hex, sizeof(password_hex)) == 0)
        {
            vm_mysql_exec("ROLLBACK");
            vm_autotest_note("mock_account_db_mysql_save_failed reason=%s error=invalid-account-record index=%u\n",
                             reason ? reason : "state", i);
            return;
        }
        snprintf(query, sizeof(query),
                 "INSERT INTO accounts(account_id,password_value) VALUES(CAST(X'%s' AS CHAR),X'%s')",
                 username_hex, password_hex);
        if (!vm_mysql_exec(query))
        {
            vm_autotest_note("mock_account_db_mysql_save_failed reason=%s index=%u error=%s\n",
                             reason ? reason : "state", i, vm_mysql_last_error());
            vm_mysql_exec("ROLLBACK");
            return;
        }
    }
    if (!vm_mysql_exec("COMMIT"))
    {
        vm_autotest_note("mock_account_db_mysql_save_failed reason=%s error=%s\n",
                         reason ? reason : "state", vm_mysql_last_error());
        return;
    }
    vm_autotest_note("mock_account_db_mysql_save reason=%s count=%u\n",
                     reason ? reason : "state",
                     g_vm_mock_service_account_db.accountCount);
}

static void vm_mock_service_account_db_load(void)
{
    if (g_vm_mock_service_account_db_loaded)
        return;
    g_vm_mock_service_account_db_loaded = true;
    memset(&g_vm_mock_service_account_db, 0, sizeof(g_vm_mock_service_account_db));
    memcpy(g_vm_mock_service_account_db.magic, "JHA1", 4);
    g_vm_mock_service_account_db.version = VM_MOCK_SERVICE_ACCOUNT_DB_VERSION;
    g_vm_mock_service_account_db_valid = true;

    vm_mock_mysql_account_load_context context;
    memset(&context, 0, sizeof(context));
    context.database = &g_vm_mock_service_account_db;
    if (!vm_mysql_query("SELECT account_id,HEX(password_value) FROM accounts ORDER BY account_id",
                        vm_mock_mysql_account_row, &context))
    {
        g_vm_mock_service_account_db_valid = false;
        vm_autotest_note("mock_account_db_mysql_load_failed error=%s\n", vm_mysql_last_error());
        return;
    }
    if (context.invalid)
    {
        memset(&g_vm_mock_service_account_db, 0, sizeof(g_vm_mock_service_account_db));
        g_vm_mock_service_account_db_valid = false;
        vm_autotest_note("mock_account_db_mysql_load_failed error=invalid-row\n");
        return;
    }
    if (g_vm_mock_service_account_db.accountCount == 0 && vm_mock_service_account_db_load_legacy())
    {
        vm_autotest_note("mock_account_db_legacy_migrate count=%u\n",
                         g_vm_mock_service_account_db.accountCount);
        vm_mock_service_account_db_save("legacy-migrate");
    }
    vm_autotest_note("mock_account_db_mysql_load count=%u\n",
                     g_vm_mock_service_account_db.accountCount);
}

static void vm_mock_service_friend_db_path(char *path, size_t pathSize)
{
    if (path == NULL || pathSize == 0)
        return;
    snprintf(path, pathSize, "nvram/mock_service_friends.bin");
}

static void vm_mock_service_friend_db_save(const char *reason)
{
    if (!g_vm_mock_service_friend_db_valid)
        return;
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
        return;
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
            return;
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
            return;
        }
    }
    if (!vm_mysql_exec("COMMIT"))
    {
        vm_autotest_note("mock_friend_db_mysql_save_failed reason=%s error=%s\n",
                         reason ? reason : "state", vm_mysql_last_error());
        return;
    }
    vm_autotest_note("mock_friend_db_mysql_save reason=%s records=%u\n",
                     reason ? reason : "state",
                     g_vm_mock_service_friend_db.recordCount);
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
    if (needsSave)
        vm_mock_service_friend_db_save(loadedFromFile ? "legacy-migrate" : "normalize");
    vm_autotest_note("mock_friend_db_mysql_load records=%u\n",
                     g_vm_mock_service_friend_db.recordCount);
}

static vm_mock_service_friend_record *vm_mock_service_friend_db_find(
    const char *ownerAccountId, u32 ownerRoleId,
    const char *targetAccountId, u32 targetRoleId)
{
    vm_mock_service_friend_db_load();
    if (!g_vm_mock_service_friend_db_valid || ownerAccountId == NULL ||
        targetAccountId == NULL || ownerRoleId == 0 || targetRoleId == 0)
    {
        return NULL;
    }
    for (u32 i = 0; i < g_vm_mock_service_friend_db.recordCount; ++i)
    {
        vm_mock_service_friend_record *record = &g_vm_mock_service_friend_db.records[i];
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

static bool vm_mock_service_friend_db_upsert_one(
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
    record = vm_mock_service_friend_db_find(ownerAccountId, ownerRoleId,
                                            targetAccountId, targetRoleId);
    if (record == NULL)
    {
        if (g_vm_mock_service_friend_db.recordCount >= VM_MOCK_SERVICE_FRIEND_DB_MAX_RECORDS)
            return false;
        record = &g_vm_mock_service_friend_db.records[g_vm_mock_service_friend_db.recordCount++];
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

static bool vm_mock_service_friend_db_add_pair(
    const char *ownerAccountId, u32 ownerRoleId,
    const char *ownerRoleName, u32 ownerLevel, u8 ownerJob, u8 ownerSex,
    const char *targetAccountId, u32 targetRoleId,
    const char *targetRoleName, u32 targetLevel, u8 targetJob, u8 targetSex,
    bool *createdOut)
{
    bool forwardExists = false;
    bool reverseExists = false;
    bool forwardCreated = false;
    bool reverseCreated = false;
    bool forwardChanged = false;
    bool reverseChanged = false;
    u32 missing = 0;

    if (createdOut)
        *createdOut = false;
    if (ownerAccountId == NULL || ownerAccountId[0] == 0 || ownerRoleId == 0 ||
        targetAccountId == NULL || targetAccountId[0] == 0 || targetRoleId == 0 ||
        (ownerRoleId == targetRoleId && strcmp(ownerAccountId, targetAccountId) == 0))
    {
        return false;
    }
    vm_mock_service_friend_db_load();
    forwardExists = vm_mock_service_friend_db_find(ownerAccountId, ownerRoleId,
                                                    targetAccountId, targetRoleId) != NULL;
    reverseExists = vm_mock_service_friend_db_find(targetAccountId, targetRoleId,
                                                    ownerAccountId, ownerRoleId) != NULL;
    missing = (forwardExists ? 0u : 1u) + (reverseExists ? 0u : 1u);
    if (!g_vm_mock_service_friend_db_valid ||
        g_vm_mock_service_friend_db.recordCount + missing > VM_MOCK_SERVICE_FRIEND_DB_MAX_RECORDS)
    {
        return false;
    }
    if (!vm_mock_service_friend_db_upsert_one(ownerAccountId, ownerRoleId,
                                              targetAccountId, targetRoleId,
                                              targetRoleName, targetLevel, targetJob, targetSex,
                                              &forwardCreated, &forwardChanged) ||
        !vm_mock_service_friend_db_upsert_one(targetAccountId, targetRoleId,
                                              ownerAccountId, ownerRoleId,
                                              ownerRoleName, ownerLevel, ownerJob, ownerSex,
                                              &reverseCreated, &reverseChanged))
    {
        return false;
    }
    if (forwardChanged || reverseChanged)
        vm_mock_service_friend_db_save("friend-invite-accepted");
    if (createdOut)
        *createdOut = forwardCreated || reverseCreated;
    return true;
}

static vm_mock_service_account_record *vm_mock_service_account_find_record(const char *username)
{
    vm_mock_service_account_db_load();
    if (!g_vm_mock_service_account_db_valid || username == NULL || username[0] == 0)
        return NULL;
    for (u32 i = 0; i < g_vm_mock_service_account_db.accountCount; ++i)
    {
        if (strcmp(g_vm_mock_service_account_db.accounts[i].username, username) == 0)
            return &g_vm_mock_service_account_db.accounts[i];
    }
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

static bool vm_mock_service_account_create_record(const char *username,
                                                  const char *password,
                                                  const char **messageOut)
{
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
    if (g_vm_mock_service_account_db.accountCount >= VM_MOCK_SERVICE_ACCOUNT_DB_MAX_ACCOUNTS)
    {
        if (messageOut)
            *messageOut = "account db full";
        return false;
    }
    vm_mock_service_account_record *record =
        &g_vm_mock_service_account_db.accounts[g_vm_mock_service_account_db.accountCount++];
    memset(record, 0, sizeof(*record));
    snprintf(record->username, sizeof(record->username), "%s", username);
    snprintf(record->password, sizeof(record->password), "%s", password);
    vm_mock_service_account_db_save("create");
    return true;
}

static bool vm_mock_service_account_set_password(const char *username,
                                                 const char *password,
                                                 const char **messageOut)
{
    vm_mock_service_account_record *record = vm_mock_service_account_find_record(username);
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
    snprintf(record->password, sizeof(record->password), "%s", password);
    vm_mock_service_account_db_save("passwd");
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
    const char *message = "account db unavailable";

    if (messageOut)
        *messageOut = message;
    if (usernameOut == NULL || usernameOutCap == 0 || passwordOut == NULL || passwordOutCap == 0)
        return false;
    usernameOut[0] = 0;
    passwordOut[0] = 0;

    vm_mock_service_account_db_load();
    if (!g_vm_mock_service_account_db_valid)
        return false;

    seedBase = g_vm_mock_service_account_db.accountCount + 1;
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

static u32 vm_net_mock_role_level_start_exp(u32 level)
{
    unsigned long long a = (unsigned long long)(level - 1);
    unsigned long long b = (unsigned long long)level;
    unsigned long long startExp;

    if (level <= 1)
        return 0;
    if ((a & 1ull) == 0)
        a /= 2;
    else
        b /= 2;
    if (a != 0 && b > 0xffffffffull / a)
        return 0xffffffffu;
    startExp = a * b;
    if (startExp > 0xffffffffull / VM_NET_MOCK_ROLE_EXP_PER_LEVEL)
        return 0xffffffffu;
    startExp *= VM_NET_MOCK_ROLE_EXP_PER_LEVEL;
    if (startExp > 0xffffffffull)
        return 0xffffffffu;
    return (u32)startExp;
}

static u32 vm_net_mock_role_level_from_exp(u32 exp)
{
    u32 level = 1;

    for (;;)
    {
        u32 nextLevel = level + 1;
        u32 nextLevelStart;

        if (nextLevel == 0)
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

    if (nextLevel == 0)
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

typedef struct
{
    u8 id;
    u8 fieldB;
    u32 minMoney;
    const char *name;
    const char *description;
    const char *overheadResource;
} vm_net_mock_designation_entry;

static const vm_net_mock_designation_entry g_vm_net_mock_designation_entries[] = {
    {
        0,
        0,
        0,
        "\xd2\xbb\xc6\xb6\xc8\xe7\xcf\xb4", /* GBK: yi pin ru xi */
        "\xc5\xcc\xb2\xf8\xb2\xbb\xd7\xe3", /* GBK: pan chan bu zu */
        "riches_name0.gif",
    },
    {
        1,
        0,
        5000,
        "\xd2\xc2\xca\xb3\xce\xde\xd3\xc7", /* GBK: yi shi wu you */
        "\xc2\xd4\xd3\xd0\xbb\xfd\xd0\xee", /* GBK: lue you ji xu */
        "riches_name1.gif",
    },
    {
        2,
        0,
        20000,
        "\xc9\xfa\xb2\xc6\xd3\xd0\xb5\xc0", /* GBK: sheng cai you dao */
        "\xd0\xa1\xd3\xd0\xd7\xca\xb2\xfa", /* GBK: xiao you zi chan */
        "riches_name2.gif",
    },
    {
        3,
        0,
        50000,
        "\xc0\xed\xb2\xc6\xd3\xd0\xb7\xbd", /* GBK: li cai you fang */
        "\xb2\xc6\xc2\xb7\xbd\xa5\xbf\xed", /* GBK: cai lu jian kuan */
        "riches_name3.gif",
    },
    {
        4,
        0,
        100000,
        "\xb2\xc6\xd4\xcb\xba\xe0\xcd\xa8", /* GBK: cai yun heng tong */
        "\xc7\xae\xb2\xc6\xb7\xe1\xba\xf1", /* GBK: qian cai feng hou */
        "riches_name4.gif",
    },
    {
        5,
        0,
        300000,
        "\xd1\xfc\xb2\xf8\xcd\xf2\xb9\xe1", /* GBK: yao chan wan guan */
        "\xbb\xd3\xbd\xf0\xd3\xd0\xb6\xc8", /* GBK: hui jin you du */
        "riches_name5.gif",
    },
    {
        6,
        0,
        500000,
        "\xbc\xd2\xb2\xc6\xcd\xf2\xb9\xe1", /* GBK: jia cai wan guan */
        "\xb2\xc6\xb8\xbb\xbe\xaa\xc8\xcb", /* GBK: cai fu jing ren */
        "riches_name6.gif",
    },
    {
        7,
        0,
        1000000,
        "\xb8\xbb\xc9\xcc\xbe\xde\xbc\xd6", /* GBK: fu shang ju gu */
        "\xc9\xcc\xbc\xd6\xce\xc5\xc3\xfb", /* GBK: shang gu wen ming */
        "riches_name7.gif",
    },
    {
        8,
        0,
        3000000,
        "\xb8\xbb\xbc\xd7\xd2\xbb\xb7\xbd", /* GBK: fu jia yi fang */
        "\xb2\xc6\xb9\xda\xd2\xbb\xb7\xbd", /* GBK: cai guan yi fang */
        "riches_name8.gif",
    },
    {
        9,
        0,
        10000000,
        "\xb8\xbb\xbf\xc9\xb5\xd0\xb9\xfa", /* GBK: fu ke di guo */
        "\xcc\xec\xcf\xc2\xbe\xde\xb8\xbb", /* GBK: tian xia ju fu */
        "riches_name9.gif",
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

static bool vm_net_mock_designation_is_unlocked(const vm_net_mock_role_state *role,
                                                const vm_net_mock_designation_entry *entry)
{
    u32 money = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    if (entry == NULL)
        return false;
    return money >= entry->minMoney;
}

static const vm_net_mock_designation_entry *vm_net_mock_role_best_designation(const vm_net_mock_role_state *role)
{
    const vm_net_mock_designation_entry *best = &g_vm_net_mock_designation_entries[0];
    u32 count = vm_net_mock_designation_entry_count();
    for (u32 i = 0; i < count; ++i)
    {
        const vm_net_mock_designation_entry *entry = &g_vm_net_mock_designation_entries[i];
        if (vm_net_mock_designation_is_unlocked(role, entry))
            best = entry;
    }
    return best;
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
        snprintf(nameOut, nameOutSize, "%s", "\xce\xde\xb0\xef\xc5\xc9"); /* GBK: 无帮派 */
    }
    if (overrideName != NULL && overrideName[0] != 0)
        snprintf(nameOut, nameOutSize, "%s", overrideName);
    return guildId;
}

static const char *vm_net_mock_role_spouse_name(const vm_net_mock_role_state *role)
{
    (void)role;
    return vm_net_mock_env_str("CBE_ACTOR_SPOUSE_NAME",
                               "\xce\xde"); /* GBK: wu */
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
    if (value > 999)
        value = 999;
    return (u16)value;
}

static u16 vm_net_mock_role_charm(const vm_net_mock_role_state *role, u32 level, u32 job)
{
    u32 money = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    u32 value = vm_net_mock_role_derived_attr(level, job, 4) + money / 100000;
    if (value > 999)
        value = 999;
    return (u16)value;
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

static void vm_net_mock_role_collect_equipment_bonus(const vm_net_mock_role_state *role,
                                                     u32 level,
                                                     vm_net_mock_equipment_bonus *bonus)
{
    if (bonus == NULL)
        return;
    memset(bonus, 0, sizeof(*bonus));
    if (role == NULL)
        return;
    if (level == 0)
        level = 1;
    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        u32 itemId = role->equippedItemIds[slot];
        const vm_net_mock_equipment_catalog_item *item = NULL;

        if (itemId == 0)
            continue;
        item = vm_net_mock_find_equipment_catalog_item(itemId);
        if (item == NULL || item->slot != slot)
            continue;
        if (item->levelRequired > level)
            continue;
        vm_net_mock_equipment_bonus_add(bonus, &item->bonus);
    }
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
    stats->baseCharm = vm_net_mock_role_derived_attr(level, job, 4);
    stats->strength = vm_net_mock_cap_u32(stats->baseStrength + equipment.strength, 999);
    stats->agility = vm_net_mock_cap_u32(stats->baseAgility + equipment.agility, 999);
    stats->wisdom = vm_net_mock_cap_u32(stats->baseWisdom + equipment.wisdom, 999);
    stats->endurance = vm_net_mock_cap_u32(stats->baseEndurance, 999);
    stats->charm = vm_net_mock_cap_u32(vm_net_mock_role_charm(role, level, job), 999);

    stats->maxHp = 90 + level * 8 + stats->endurance * 2 + equipment.hp;
    stats->maxMp = 70 + level * 9 + stats->wisdom * 3 + equipment.mp;
    stats->attack = 6 + level * 2 + stats->strength / 2 + equipment.attack / 3;
    stats->defense = 4 + level + stats->endurance / 2 + equipment.armor / 5;
    stats->hit = 75 + level + stats->agility * 2 + equipment.hit;
    stats->dodge = 3 + level / 2 + stats->agility / 2 + equipment.dodge / 2;
    stats->crit = 1 + stats->agility / 3 + stats->wisdom / 5 + equipment.crit / 2;
    stats->resist = stats->wisdom / 2 + stats->endurance / 3 + equipment.resist;

    stats->maxHp = vm_net_mock_cap_u32(stats->maxHp, 9999);
    stats->maxMp = vm_net_mock_cap_u32(stats->maxMp, 9999);
    stats->attack = vm_net_mock_cap_u32(stats->attack, 9999);
    stats->defense = vm_net_mock_cap_u32(stats->defense, 9999);
    stats->hit = vm_net_mock_cap_u32(stats->hit, 9999);
    stats->dodge = vm_net_mock_cap_u32(stats->dodge, 9999);
    stats->crit = vm_net_mock_cap_u32(stats->crit, 9999);
    stats->resist = vm_net_mock_cap_u32(stats->resist, 9999);
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

static u32 vm_net_mock_damage_after_defense(u32 attack, u32 defense)
{
    uint64_t scaled = 0;

    if (attack == 0)
        attack = 1;
    scaled = ((uint64_t)attack * 100ull + defense / 2u) / (100u + defense);
    if (scaled == 0)
        scaled = 1;
    if (scaled > 0xffffffffull)
        scaled = 0xffffffffull;
    return (u32)scaled;
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
    u32 enemyId;
    u32 level;
    u32 hp;
    u32 mp;
    u32 attack;
    u32 defense;
    u32 exp;
    u32 gold;
    u32 dropItemId;
    u32 dropRatePercent;
} vm_net_mock_monster_stats;

typedef struct
{
    bool used;
    u8 family;
    vm_net_mock_monster_stats stats;
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
    u32 dropItemId;
    u32 dropRatePercent;
    u8 family;
    bool overridden;
    char displayName[32];
    char firstScene[64];
} vm_net_mock_monster_admin_row;

static const vm_net_mock_monster_entry g_vm_net_mock_monster_entries[] = {
    {  1,  6, VM_NET_MOCK_MONSTER_BEAST, 27, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    {  3,  1, VM_NET_MOCK_MONSTER_FLYING, 18, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    {  4,  2, VM_NET_MOCK_MONSTER_INSECT, 0, 0},
    {  6,  7, VM_NET_MOCK_MONSTER_BEAST, 29, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    {  9,  3, VM_NET_MOCK_MONSTER_BEAST, 25, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 13, 12, VM_NET_MOCK_MONSTER_BOSS, 32, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 15, 55, VM_NET_MOCK_MONSTER_REPTILE, 34, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 18, 38, VM_NET_MOCK_MONSTER_ELEMENTAL, 36, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 19, 28, VM_NET_MOCK_MONSTER_BOSS, 37, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 22, 12, VM_NET_MOCK_MONSTER_SPIRIT, 53, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 25,  4, VM_NET_MOCK_MONSTER_UNDEAD, 43, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 28,  7, VM_NET_MOCK_MONSTER_FLYING, 45, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 29,  8, VM_NET_MOCK_MONSTER_FLYING, 0, 0},
    { 30,  8, VM_NET_MOCK_MONSTER_STONE, 47, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 31,  9, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
    { 32, 10, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    { 34, 11, VM_NET_MOCK_MONSTER_UNDEAD, 51, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 36, 14, VM_NET_MOCK_MONSTER_STONE, 52, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 40, 20, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    { 41, 20, VM_NET_MOCK_MONSTER_SLIME, 55, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 42, 22, VM_NET_MOCK_MONSTER_ELEMENTAL, 56, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
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
    { 73,  5, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 74,  5, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 75,  6, VM_NET_MOCK_MONSTER_UNDEAD, 0, 0},
    { 76,  3, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    { 77,  4, VM_NET_MOCK_MONSTER_UNDEAD, 28, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 78,  9, VM_NET_MOCK_MONSTER_UNDEAD, 50, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 79, 11, VM_NET_MOCK_MONSTER_STONE, 49, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 81, 13, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    { 82, 15, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    { 83, 15, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    { 84, 17, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    { 86, 17, VM_NET_MOCK_MONSTER_SOLDIER, 0, 0},
    { 87, 17, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
    { 89, 18, VM_NET_MOCK_MONSTER_SOLDIER, 0, 0},
    { 91, 21, VM_NET_MOCK_MONSTER_INSECT, 0, 0},
    { 92, 23, VM_NET_MOCK_MONSTER_SLIME, 57, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 94, 26, VM_NET_MOCK_MONSTER_INSECT, 59, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 97, 36, VM_NET_MOCK_MONSTER_ELEMENTAL, 65, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 98, 38, VM_NET_MOCK_MONSTER_SLIME, 0, 0},
    { 99, 39, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    {101, 39, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
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

static vm_net_mock_monster_override
    g_vm_net_mock_monster_overrides[sizeof(g_vm_net_mock_monster_entries) /
                                    sizeof(g_vm_net_mock_monster_entries[0])];
static bool g_vm_net_mock_monster_db_loaded = false;
static bool g_vm_net_mock_monster_db_valid = false;

static bool vm_net_mock_monster_enemy_id_known(u32 enemyId)
{
    if (enemyId == 0 || enemyId > 0xffffu)
        return false;
    for (u32 i = 0;
         i < sizeof(g_vm_net_mock_monster_entries) /
                 sizeof(g_vm_net_mock_monster_entries[0]);
         ++i)
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
    for (u32 i = 0; i < sizeof(g_vm_net_mock_monster_entries) / sizeof(g_vm_net_mock_monster_entries[0]); ++i)
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
    stats.dropItemId = entry.dropItemId;
    stats.dropRatePercent = entry.dropRatePercent;

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

static int vm_net_mock_monster_catalog_index(u32 enemyId)
{
    for (u32 i = 0;
         i < sizeof(g_vm_net_mock_monster_entries) /
                 sizeof(g_vm_net_mock_monster_entries[0]);
         ++i)
    {
        if (g_vm_net_mock_monster_entries[i].enemyId == enemyId)
            return (int)i;
    }
    return -1;
}

typedef struct
{
    u32 loaded;
    u32 skipped;
} vm_net_mock_monster_db_load_context;

static bool vm_net_mock_monster_db_row(void *contextValue,
                                       unsigned int columnCount,
                                       const char *const *values,
                                       const size_t *lengths)
{
    vm_net_mock_monster_db_load_context *context =
        (vm_net_mock_monster_db_load_context *)contextValue;
    u32 number[11];
    int index = -1;
    vm_net_mock_monster_override *override = NULL;

    memset(number, 0, sizeof(number));
    if (context == NULL || columnCount != 11)
        return false;
    for (u32 i = 0; i < 11; ++i)
    {
        if (!vm_mock_mysql_parse_u32(values[i], lengths[i], &number[i]))
        {
            ++context->skipped;
            return true;
        }
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
        number[10] > 100u ||
        ((number[9] == 0) != (number[10] == 0)))
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
    override->stats.dropItemId = number[9];
    override->stats.dropRatePercent = number[10];
    ++context->loaded;
    return true;
}

static bool vm_net_mock_monster_db_load(void)
{
    vm_net_mock_monster_db_load_context context;

    if (g_vm_net_mock_monster_db_loaded)
        return g_vm_net_mock_monster_db_valid;
    g_vm_net_mock_monster_db_loaded = true;
    g_vm_net_mock_monster_db_valid = false;
    memset(g_vm_net_mock_monster_overrides, 0,
           sizeof(g_vm_net_mock_monster_overrides));
    memset(&context, 0, sizeof(context));

    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_monsters ("
            "monster_id SMALLINT UNSIGNED NOT NULL,level TINYINT UNSIGNED NOT NULL,"
            "family TINYINT UNSIGNED NOT NULL,hp INT UNSIGNED NOT NULL,"
            "mp INT UNSIGNED NOT NULL,attack_value INT UNSIGNED NOT NULL,"
            "defense_value INT UNSIGNED NOT NULL,reward_exp INT UNSIGNED NOT NULL,"
            "reward_money INT UNSIGNED NOT NULL,drop_item_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "drop_rate_percent TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(monster_id)) ENGINE=InnoDB") ||
        !vm_mysql_query(
            "SELECT monster_id,level,family,hp,mp,attack_value,defense_value,"
            "reward_exp,reward_money,drop_item_id,drop_rate_percent "
            "FROM server_monsters ORDER BY monster_id",
            vm_net_mock_monster_db_row, &context))
    {
        printf("[error][mock-admin] monster_db_load failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_monster_db_valid = true;
    printf("[info][mock-admin] monster_db_load rows=%u skipped=%u\n",
           context.loaded, context.skipped);
    return true;
}

static vm_net_mock_monster_stats vm_net_mock_monster_stats_for_enemy(u32 enemyId)
{
    int index = vm_net_mock_monster_catalog_index(enemyId);

    (void)vm_net_mock_monster_db_load();
    if (index >= 0 && g_vm_net_mock_monster_overrides[index].used)
        return g_vm_net_mock_monster_overrides[index].stats;
    return vm_net_mock_monster_base_stats_for_enemy(enemyId);
}

static bool vm_net_mock_monster_admin_save(
    const vm_net_mock_monster_admin_row *row, const char **errorOut)
{
    char query[1024];
    int index = -1;
    vm_net_mock_monster_override *override = NULL;

    if (errorOut)
        *errorOut = "怪物属性无效";
    if (row == NULL || row->enemyId == 0 || row->level == 0 ||
        row->level > 0xffu || row->family > VM_NET_MOCK_MONSTER_BOSS ||
        row->hp == 0 || row->mp == 0 || row->attack == 0 ||
        row->hp > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->mp > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->attack > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->defense > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->exp > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->gold > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX ||
        row->dropRatePercent > 100u ||
        ((row->dropItemId == 0) != (row->dropRatePercent == 0)))
    {
        return false;
    }
    index = vm_net_mock_monster_catalog_index(row->enemyId);
    if (index < 0)
    {
        if (errorOut)
            *errorOut = "怪物目录中不存在该 ID";
        return false;
    }
    if (row->dropItemId != 0 &&
        vm_net_mock_find_shop_catalog_item(row->dropItemId) == NULL)
    {
        if (errorOut)
            *errorOut = "掉落物品 ID 不在物品目录中";
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

    snprintf(
        query, sizeof(query),
        "INSERT INTO server_monsters(monster_id,level,family,hp,mp,attack_value,"
        "defense_value,reward_exp,reward_money,drop_item_id,drop_rate_percent) "
        "VALUES(%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u) ON DUPLICATE KEY UPDATE "
        "level=VALUES(level),family=VALUES(family),hp=VALUES(hp),mp=VALUES(mp),"
        "attack_value=VALUES(attack_value),defense_value=VALUES(defense_value),"
        "reward_exp=VALUES(reward_exp),reward_money=VALUES(reward_money),"
        "drop_item_id=VALUES(drop_item_id),drop_rate_percent=VALUES(drop_rate_percent)",
        row->enemyId, row->level, row->family, row->hp, row->mp,
        row->attack, row->defense, row->exp, row->gold,
        row->dropItemId, row->dropRatePercent);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }

    override = &g_vm_net_mock_monster_overrides[index];
    memset(override, 0, sizeof(*override));
    override->used = true;
    override->family = (u8)row->family;
    override->stats.enemyId = row->enemyId;
    override->stats.level = row->level;
    override->stats.hp = row->hp;
    override->stats.mp = row->mp;
    override->stats.attack = row->attack;
    override->stats.defense = row->defense;
    override->stats.exp = row->exp;
    override->stats.gold = row->gold;
    override->stats.dropItemId = row->dropItemId;
    override->stats.dropRatePercent = row->dropRatePercent;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] monster_save id=%u level=%u family=%u hp=%u mp=%u attack=%u defense=%u exp=%u money=%u drop=%u rate=%u\n",
           row->enemyId, row->level, row->family, row->hp, row->mp,
           row->attack, row->defense, row->exp, row->gold,
           row->dropItemId, row->dropRatePercent);
    return true;
}

static bool vm_net_mock_monster_admin_reset(u32 enemyId,
                                            const char **errorOut)
{
    char query[256];
    int index = vm_net_mock_monster_catalog_index(enemyId);

    if (errorOut)
        *errorOut = "怪物目录中不存在该 ID";
    if (index < 0)
        return false;
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
    snprintf(query, sizeof(query),
             "DELETE FROM server_monsters WHERE monster_id=%u", enemyId);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    memset(&g_vm_net_mock_monster_overrides[index], 0,
           sizeof(g_vm_net_mock_monster_overrides[index]));
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] monster_reset id=%u source=server-default\n",
           enemyId);
    return true;
}

static u32 vm_net_mock_battle_role_attack_default(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_player_stats stats;
    vm_net_mock_role_build_player_stats(role, &stats);
    return stats.attack ? stats.attack : 1;
}

static u32 vm_net_mock_battle_role_defense_default(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_player_stats stats;
    vm_net_mock_role_build_player_stats(role, &stats);
    return stats.defense;
}

static u32 vm_net_mock_battle_player_damage_to_enemy(u32 enemyId, u32 enemyHpCurrent)
{
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(enemyId);
    u32 attack = vm_net_mock_env_u32_if_set("CBE_BATTLE_PLAYER_ATTACK",
                                            vm_net_mock_battle_role_attack_default());
    u32 defense = vm_net_mock_env_u32_if_set("CBE_BATTLE_ENEMY_DEFENSE", stats.defense);
    u32 damage = vm_net_mock_damage_after_defense(attack, defense);

    if (enemyHpCurrent == 0)
        return 0;
    if (damage == 0)
        damage = 1;
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
    u32 defense = 0;
    u32 damage = 0;

    if (enemyHpCurrent == 0)
        return 0;
    if (skill == NULL || baseDamage == 0)
        return vm_net_mock_battle_player_damage_to_enemy(enemyId, enemyHpCurrent);

    vm_net_mock_role_build_player_stats(role, &playerStats);
    coeffDamage += (uint64_t)playerStats.strength * skill->strengthCoeff;
    coeffDamage += (uint64_t)playerStats.agility * skill->agilityCoeff;
    coeffDamage += (uint64_t)playerStats.wisdom * skill->wisdomCoeff;
    coeffDamage = (coeffDamage + 50u) / 100u;
    if (coeffDamage > 0xffffffffull - baseDamage)
        rawDamage = 0xffffffffu;
    else
        rawDamage = baseDamage + (u32)coeffDamage;

    defense = vm_net_mock_env_u32_if_set("CBE_BATTLE_SKILL_ENEMY_DEFENSE",
                                         monsterStats.defense);
    damage = vm_net_mock_damage_after_defense(rawDamage, defense);
    if (damage < baseDamage)
        damage = baseDamage;
    damage = vm_net_mock_env_u32_if_set("CBE_BATTLE_SKILL_DAMAGE", damage);
    if (damage == 0)
        damage = 1;
    return vm_net_mock_min_u32(damage, enemyHpCurrent);
}

static u32 vm_net_mock_battle_enemy_damage_to_role(u32 enemyId, u32 roleHpCurrent)
{
    vm_net_mock_monster_stats stats = vm_net_mock_monster_stats_for_enemy(enemyId);
    u32 attack = vm_net_mock_env_u32_if_set("CBE_BATTLE_ENEMY_ATTACK", stats.attack);
    u32 defense = vm_net_mock_env_u32_if_set("CBE_BATTLE_ROLE_DEFENSE",
                                             vm_net_mock_battle_role_defense_default());
    u32 damage = vm_net_mock_damage_after_defense(attack, defense);

    if (roleHpCurrent == 0)
        return 0;
    if (damage == 0)
        damage = 1;
    return vm_net_mock_min_u32(damage, roleHpCurrent);
}

static const char *vm_net_mock_role_initial_scene_name(void)
{
    return vm_net_mock_default_scene_name();
}

static bool vm_net_mock_role_canonicalize_initial_scene(vm_net_mock_role_state *role)
{
    static const char legacyInitialScene[] =
        "\x30\x30\x5f\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x30\x31"; /* 00_蓬莱仙岛01 */
    static const char legacyInitialSceneWithSuffix[] =
        "\x30\x30\x5f\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x30\x31\x2e\x73\x63\x65"; /* 00_蓬莱仙岛01.sce */
    static const char canonicalInitialSceneWithoutSuffix[] =
        "\x63\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x31"; /* c00蓬莱仙岛_01 */

    if (role == NULL ||
        (strcmp(role->scene, legacyInitialScene) != 0 &&
         strcmp(role->scene, legacyInitialSceneWithSuffix) != 0 &&
         strcmp(role->scene, canonicalInitialSceneWithoutSuffix) != 0))
    {
        return false;
    }
    snprintf(role->scene, sizeof(role->scene), "%s",
             vm_net_mock_role_initial_scene_name());
    return true;
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

static void vm_net_mock_role_init_default_equipment(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    memset(role->equippedItemIds, 0, sizeof(role->equippedItemIds));
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
    u16 maxSeq = 0;

    if (role == NULL)
        return;
    memset(compact, 0, sizeof(compact));
    if (role->backpackCapacity == 0)
        role->backpackCapacity = VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY;
    else if (role->backpackCapacity > VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT)
        role->backpackCapacity = VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT;
    if (role->backpackItemCount > VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        role->backpackItemCount = VM_NET_MOCK_BACKPACK_MAX_ITEMS;
    if (role->backpackItemCount > role->backpackCapacity)
        role->backpackItemCount = role->backpackCapacity;

    for (u32 i = 0; i < role->backpackItemCount; ++i)
    {
        vm_net_mock_backpack_item_state item = role->backpackItems[i];
        if (item.itemId == 0 || item.count == 0)
            continue;
        if (item.enhanceLevel > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
            item.enhanceLevel = VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL;
        if (item.seq == 0)
            item.seq = (u16)(maxSeq + 1);
        if (item.seq > maxSeq)
            maxSeq = item.seq;
        compact[compactCount++] = item;
        if (compactCount >= role->backpackCapacity ||
            compactCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        {
            break;
        }
    }
    memset(role->backpackItems, 0, sizeof(role->backpackItems));
    if (compactCount > 0)
        memcpy(role->backpackItems, compact, sizeof(compact[0]) * compactCount);
    role->backpackItemCount = (u8)compactCount;
    if (role->nextBackpackSeq == 0 || role->nextBackpackSeq <= maxSeq)
        role->nextBackpackSeq = (u16)(maxSeq + 1);
    if (role->nextBackpackSeq == 0)
        role->nextBackpackSeq = 1;
}

static void vm_net_mock_role_normalize(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
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
    if (role->designationId >= VM_NET_MOCK_ROLE_DESIGNATION_COUNT)
        role->designationId = 0;
    if (!vm_net_mock_designation_is_unlocked(role, vm_net_mock_designation_by_id(role->designationId)))
        role->designationId = vm_net_mock_role_best_designation(role)->id;
    role->level = vm_net_mock_role_level_from_exp(role->exp);
    vm_net_mock_role_sync_derived_vitals(role);
    role->scene[sizeof(role->scene) - 1] = 0;
    (void)vm_net_mock_role_canonicalize_initial_scene(role);
    if (!vm_net_mock_scene_name_is_safe(role->scene))
        snprintf(role->scene, sizeof(role->scene), "%s", vm_net_mock_role_initial_scene_name());
    if (role->x == 0 || role->y == 0)
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
    }
    vm_net_mock_adjust_safe_player_pos_for_scene(role->scene, &role->x, &role->y);
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
        format_version != VM_NET_MOCK_ROLE_DB_VERSION ||
        role_count > VM_NET_MOCK_ROLE_DB_MAX_ROLES)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    memcpy(context->database->magic, "JHR1", 4);
    context->database->version = format_version;
    context->database->activeRoleId = active_role_id;
    context->database->roleCount = role_count;
    context->found = true;
    return true;
}

static bool vm_mock_mysql_role_detail_row(void *context_value,
                                          unsigned int column_count,
                                          const char *const *values,
                                          const size_t *lengths)
{
    vm_mock_mysql_role_load_context *context = (vm_mock_mysql_role_load_context *)context_value;
    u32 number[18];
    size_t name_len = 0;
    size_t scene_len = 0;
    memset(number, 0, sizeof(number));
    if (context == NULL || context->database == NULL || column_count != 20 ||
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
        number[0] >= context->database->roleCount || number[0] >= VM_NET_MOCK_ROLE_DB_MAX_ROLES ||
        number[1] == 0 || number[2] > 255 || number[3] > 255 || number[4] > 255 ||
        number[13] > 65535 || number[14] > 65535 || number[15] > 255 ||
        number[16] > 255 || number[17] > 65535 ||
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
    role->backpackCapacity = (u8)number[4];
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
    role->backpackItemCount = (u8)number[15];
    role->designationId = (u8)number[16];
    role->nextBackpackSeq = (u16)number[17];
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
    if (context == NULL || context->database == NULL || column_count != 3 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &role_id) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &slot_index) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &item_id) ||
        slot_index >= VM_NET_MOCK_EQUIP_SLOT_COUNT)
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
    role->equippedItemIds[slot_index] = item_id;
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
    if (context == NULL || context->database == NULL || column_count != 6 ||
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
    role->backpackItems[slot_index].count = item_count;
    return true;
}

static bool vm_net_mock_role_db_load_mysql_relational(bool *found_out)
{
    char account_hex[129];
    char query[768];
    vm_mock_mysql_role_load_context context;
    if (found_out)
        *found_out = false;
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
    snprintf(query, sizeof(query),
             "SELECT role_index,role_id,HEX(role_name),job,sex,backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,HEX(scene),pos_x,pos_y,backpack_item_count,designation_id,next_backpack_seq "
             "FROM account_roles WHERE account_id=CAST(X'%s' AS CHAR) ORDER BY role_index",
             account_hex);
    if (!vm_mysql_query(query, vm_mock_mysql_role_detail_row, &context) || context.invalid ||
        context.roleRows != g_vm_net_mock_role_db.roleCount)
        return false;
    snprintf(query, sizeof(query),
             "SELECT role_id,slot_index,item_id FROM account_role_equipment WHERE account_id=CAST(X'%s' AS CHAR) ORDER BY role_id,slot_index",
             account_hex);
    if (!vm_mysql_query(query, vm_mock_mysql_role_equipment_row, &context) || context.invalid)
        return false;
    snprintf(query, sizeof(query),
             "SELECT role_id,slot_index,item_id,item_seq,item_count,enhance_level FROM account_role_backpack WHERE account_id=CAST(X'%s' AS CHAR) ORDER BY role_id,slot_index",
             account_hex);
    if (!vm_mysql_query(query, vm_mock_mysql_role_backpack_row, &context) || context.invalid)
        return false;
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
    if (account_id == NULL || old_ids == NULL || new_ids == NULL)
        return;
    for (u32 i = 0; i < g_vm_mock_service_friend_db.recordCount; ++i)
    {
        vm_mock_service_friend_record *record = &g_vm_mock_service_friend_db.records[i];
        for (u32 j = 0; j < mapping_count; ++j)
        {
            if (strcmp(record->ownerAccountId, account_id) == 0 && record->ownerRoleId == old_ids[j])
                record->ownerRoleId = new_ids[j];
            if (strcmp(record->targetAccountId, account_id) == 0 && record->targetRoleId == old_ids[j])
                record->targetRoleId = new_ids[j];
        }
    }
}

static bool vm_net_mock_role_db_save_relational(const char *reason,
                                                 const u32 *old_ids,
                                                 const u32 *new_ids,
                                                 u32 mapping_count)
{
    const char *account_id = g_vm_mock_service_active_account_id;
    char account_hex[129];
    char query[3072];
    char mysql_error[512];
    char *bulk_query = NULL;
    size_t bulk_capacity = 131072;
    bool transaction_started = false;
    mysql_error[0] = 0;

    if (!g_vm_net_mock_role_db_valid || !vm_net_mock_mysql_account_hex(account_hex))
        return false;
    memcpy(g_vm_net_mock_role_db.magic, "JHR1", 4);
    g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
    if (g_vm_net_mock_role_db.roleCount > VM_NET_MOCK_ROLE_DB_MAX_ROLES)
        g_vm_net_mock_role_db.roleCount = VM_NET_MOCK_ROLE_DB_MAX_ROLES;
    if (g_vm_net_mock_role_db.roleCount == 0)
        g_vm_net_mock_role_db.activeRoleId = 0;

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
             g_vm_net_mock_role_db.activeRoleId, g_vm_net_mock_role_db.roleCount);
    if (!vm_mysql_exec(query))
    {
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        goto failed;
    }
    /* Keep existing role rows alive while saving normal role state.  Guilds,
     * guild_members and guild_applications intentionally reference these rows
     * with ON DELETE CASCADE, so the former delete-and-reinsert save erased a
     * role's guild every time role-select/position/battle state was persisted.
     * Delete only roles that really disappeared from the account, move the
     * surviving role_index values out of the live range to avoid unique-index
     * collisions, then upsert them in their current order. */
    if (g_vm_net_mock_role_db.roleCount == 0)
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM account_roles WHERE account_id=CAST(X'%s' AS CHAR)",
                 account_hex);
    }
    else
    {
        size_t query_len = (size_t)snprintf(
            query, sizeof(query),
            "DELETE FROM account_roles WHERE account_id=CAST(X'%s' AS CHAR) AND role_id NOT IN (",
            account_hex);
        for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        {
            int written = snprintf(query + query_len, sizeof(query) - query_len,
                                   "%s%u", i ? "," : "",
                                   g_vm_net_mock_role_db.roles[i].roleId);
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
    if (!vm_mysql_exec(query))
    {
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        goto failed;
    }
    if (g_vm_net_mock_role_db.roleCount != 0)
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
    snprintf(query, sizeof(query),
             "DELETE FROM account_role_equipment WHERE account_id=CAST(X'%s' AS CHAR)",
             account_hex);
    if (!vm_mysql_exec(query))
    {
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        goto failed;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM account_role_backpack WHERE account_id=CAST(X'%s' AS CHAR)",
             account_hex);
    if (!vm_mysql_exec(query))
    {
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        goto failed;
    }

    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        const vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
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
                 "INSERT INTO account_roles(account_id,role_id,role_index,role_name,job,sex,backpack_capacity,level,exp,hp,hp_max,mp,mp_max,money,wcoin,scene,pos_x,pos_y,backpack_item_count,designation_id,next_backpack_seq) "
                 "VALUES(CAST(X'%s' AS CHAR),%u,%u,X'%s',%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,X'%s',%u,%u,%u,%u,%u) "
                 "ON DUPLICATE KEY UPDATE role_index=VALUES(role_index),role_name=VALUES(role_name),"
                 "job=VALUES(job),sex=VALUES(sex),backpack_capacity=VALUES(backpack_capacity),"
                 "level=VALUES(level),exp=VALUES(exp),hp=VALUES(hp),hp_max=VALUES(hp_max),"
                 "mp=VALUES(mp),mp_max=VALUES(mp_max),money=VALUES(money),wcoin=VALUES(wcoin),"
                 "scene=VALUES(scene),pos_x=VALUES(pos_x),pos_y=VALUES(pos_y),"
                 "backpack_item_count=VALUES(backpack_item_count),designation_id=VALUES(designation_id),"
                 "next_backpack_seq=VALUES(next_backpack_seq)",
                 account_hex, role->roleId, i, name_hex, role->job, role->sex,
                 role->backpackCapacity, role->level, role->exp, role->hp, role->hpMax,
                 role->mp, role->mpMax, role->money, role->wcoin, scene_hex,
                 role->x, role->y, role->backpackItemCount, role->designationId,
                 role->nextBackpackSeq);
        if (!vm_mysql_exec(query))
        {
            snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
            goto failed;
        }
    }

    size_t bulk_len = (size_t)snprintf(
        bulk_query, bulk_capacity,
        "INSERT INTO account_role_equipment(account_id,role_id,slot_index,item_id) VALUES");
    u32 bulk_rows = 0;
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        const vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
        for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
        {
            if (role->equippedItemIds[slot] == 0)
                continue;
            int written = snprintf(bulk_query + bulk_len, bulk_capacity - bulk_len,
                                   "%s(CAST(X'%s' AS CHAR),%u,%u,%u)",
                                   bulk_rows ? "," : "", account_hex, role->roleId,
                                   slot, role->equippedItemIds[slot]);
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
        "INSERT INTO account_role_backpack(account_id,role_id,slot_index,item_id,item_seq,item_count,enhance_level) VALUES");
    bulk_rows = 0;
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        const vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
        for (u32 slot = 0; slot < VM_NET_MOCK_BACKPACK_MAX_ITEMS; ++slot)
        {
            const vm_net_mock_backpack_item_state *item = &role->backpackItems[slot];
            if (item->itemId == 0 && item->seq == 0 && item->count == 0)
                continue;
            int written = snprintf(bulk_query + bulk_len, bulk_capacity - bulk_len,
                                   "%s(CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u,%u)",
                                   bulk_rows ? "," : "", account_hex, role->roleId,
                                   slot, item->itemId, item->seq, item->count,
                                   item->enhanceLevel);
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
    if (!vm_mysql_exec("COMMIT"))
    {
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        goto failed;
    }
    free(bulk_query);
    g_vm_net_mock_role_position_dirty = false;
    vm_autotest_note("mock_role_db_mysql_save account=%s reason=%s roles=%u active=%u storage=relational\n",
                     account_id ? account_id : "-", reason ? reason : "state",
                     g_vm_net_mock_role_db.roleCount, g_vm_net_mock_role_db.activeRoleId);
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

static void vm_net_mock_role_db_save(const char *reason)
{
    (void)vm_net_mock_role_db_save_relational(reason, NULL, NULL, 0);
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
    if (!loadedFromMysql &&
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
    FILE *fp = (loadedFromMysql || loadedFromPayload) ? NULL : fopen(path, "rb");
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
    {
        if (vm_net_mock_role_canonicalize_initial_scene(&g_vm_net_mock_role_db.roles[i]))
        {
            needsSave = true;
            vm_autotest_note("mock_role_initial_scene_migrate account=%s role=%u scene=c00-penglai-01.sce\n",
                             g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
                             g_vm_net_mock_role_db.roles[i].roleId);
        }
        vm_net_mock_role_normalize(&g_vm_net_mock_role_db.roles[i]);
    }
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
    g_vm_net_mock_role_db_valid = true;
    if (needsSave)
    {
        const char *saveReason = loadedFromPayload ? "payload-relational-migrate" :
                                 loadedFromFile ? "legacy-relational-migrate" :
                                 loadedFromMysql ? "normalize" : "init";
        if (!vm_net_mock_role_db_save_relational(saveReason,
                                                 migratedOldIds,
                                                 migratedNewIds,
                                                 migratedIdCount))
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
            if (g_vm_net_mock_role_db.activeRoleId != roleId)
            {
                g_vm_net_mock_role_db.activeRoleId = roleId;
                vm_net_mock_role_db_save("role-select");
            }
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
    if (!vm_net_mock_role_db_save_relational("role-create", NULL, NULL, 0))
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

    vm_net_mock_role_db_save("role-delete");
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
    if (role == NULL || scene == NULL || scene[0] == 0 || x == 0 || y == 0)
        return;
    /* Scene bootstrap reports the already persisted landing point again after
     * the map has finished initializing.  Rewriting the complete relational
     * role/equipment/backpack snapshot for that no-op costs several MySQL
     * round trips while the protocol state lock is held, delaying the very
     * response that contains the first NPC catalog and welcome message. */
    if (role->x == x && role->y == y &&
        vm_net_mock_scene_name_is_safe(role->scene) &&
        vm_net_mock_scene_names_equal_loose(role->scene, scene))
    {
        printf("[debug][mock-service] role_position_save_skip role=%u scene=%s pos=(%u,%u) reason=%s unchanged=1\n",
               role->roleId, scene, x, y, reason ? reason : "position");
        return;
    }
    snprintf(role->scene, sizeof(role->scene), "%s", scene);
    (void)vm_net_mock_role_canonicalize_initial_scene(role);
    role->x = x;
    role->y = y;
    vm_net_mock_role_normalize(role);
    vm_net_mock_role_db_save(reason ? reason : "position");
}

static void vm_net_mock_role_set_timeline_position(const char *scene,
                                                   u16 x,
                                                   u16 y,
                                                   const char *reason)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (role == NULL || !vm_net_mock_scene_name_is_safe(scene) || x == 0 || y == 0)
        return;
    /*
     * A movement timeline starts from the session's already validated scene
     * position and applies only the client's exact 4-pixel direction steps.
     * Re-running SCE landing/portal normalization here is both semantically
     * wrong (this is not a scene landing) and expensive on every upload.
     */
    snprintf(role->scene, sizeof(role->scene), "%s", scene);
    (void)vm_net_mock_role_canonicalize_initial_scene(role);
    role->x = x;
    role->y = y;
    g_vm_net_mock_role_position_dirty = true;
    (void)reason;
}

static bool vm_net_mock_role_add_backpack_item_to_role(vm_net_mock_role_state *role,
                                                        u32 itemId,
                                                        u32 count,
                                                        u16 *seqOut,
                                                        const char *reason)
{
    const vm_net_mock_item_effect_catalog_item *effect = NULL;
    u32 reservoirCapacity = 0;
    u8 itemCount = 0;

    if (seqOut)
        *seqOut = 0;
    if (role == NULL || itemId == 0 || count == 0)
        return false;

    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    effect = vm_net_mock_find_item_effect_catalog_item(itemId);
    reservoirCapacity = vm_net_mock_item_effect_reservoir_capacity(effect);
    if (reservoirCapacity != 0)
    {
        u32 freeSlots = role->backpackCapacity > itemCount
                            ? (u32)role->backpackCapacity - itemCount
                            : 0;
        u16 firstSeq = 0;

        /*
         * item.dsh marks the two vitality flasks as stack=1/consumeMode=2.
         * JianghuOL.CBE:0x10336CA stores their remaining HP/MP pool in the
         * backpack row's u32 count field, so each acquired flask needs its own
         * sequence and starts with the DSH capacity rather than count=1.
         */
        if (count > freeSlots || count > VM_NET_MOCK_BACKPACK_MAX_ITEMS - itemCount)
            return false;
        for (u32 unit = 0; unit < count; ++unit)
        {
            vm_net_mock_backpack_item_state *item = &role->backpackItems[itemCount + unit];
            memset(item, 0, sizeof(*item));
            item->itemId = itemId;
            item->seq = role->nextBackpackSeq;
            if (item->seq == 0)
                item->seq = 1;
            item->count = reservoirCapacity;
            if (firstSeq == 0)
                firstSeq = item->seq;
            role->nextBackpackSeq = (u16)(item->seq + 1);
            if (role->nextBackpackSeq == 0)
                role->nextBackpackSeq = 1;
        }
        role->backpackItemCount = (u8)(itemCount + count);
        if (seqOut)
            *seqOut = firstSeq;
        vm_net_mock_role_normalize_backpack(role);
        vm_net_mock_role_db_save(reason ? reason : "backpack-add-reservoir-item");
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
            vm_net_mock_role_db_save(reason ? reason : "backpack-add-stack");
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
    role->backpackItemCount = (u8)(itemCount + 1);
    role->nextBackpackSeq = (u16)(item->seq + 1);
    if (role->nextBackpackSeq == 0)
        role->nextBackpackSeq = 1;
    if (seqOut)
        *seqOut = item->seq;
    vm_net_mock_role_normalize_backpack(role);
    vm_net_mock_role_db_save(reason ? reason : "backpack-add-item");
    return true;
}

static bool vm_net_mock_role_add_backpack_item(u32 itemId, u32 count, u16 *seqOut)
{
    return vm_net_mock_role_add_backpack_item_to_role(
        vm_net_mock_active_role(), itemId, count, seqOut, NULL);
}

static vm_net_mock_backpack_item_state *vm_net_mock_role_find_backpack_item(vm_net_mock_role_state *role,
                                                                            u32 itemId,
                                                                            u16 seq)
{
    u8 itemCount = 0;

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
    u8 itemCount = 0;

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

    slot = equip->slot;
    oldItemId = role->equippedItemIds[slot];
    if (oldItemId != 0)
    {
        bool oldAlreadyStacked = vm_net_mock_role_find_backpack_item(role, oldItemId, 0) != NULL;
        u8 itemCount = vm_net_mock_role_backpack_count(role);
        if (!oldAlreadyStacked && !selectedFreesSlot && itemCount >= role->backpackCapacity)
        {
            if (reasonOut)
                *reasonOut = "bag-full-for-old";
            return false;
        }
    }

    if (!vm_net_mock_role_consume_backpack_item(role, itemId, seq, 1, &remaining))
    {
        if (reasonOut)
            *reasonOut = "consume-failed";
        return false;
    }
    role->equippedItemIds[slot] = itemId;
    if (oldItemId != 0)
    {
        u16 oldSeq = 0;
        if (!vm_net_mock_role_add_backpack_item(oldItemId, 1, &oldSeq))
        {
            role->equippedItemIds[slot] = oldItemId;
            (void)vm_net_mock_role_add_backpack_item(itemId, 1, NULL);
            if (reasonOut)
                *reasonOut = "old-return-failed";
            return false;
        }
    }

    vm_net_mock_role_sync_derived_vitals(role);
    vm_net_mock_role_db_save("item-equip");

    if (equippedItemIdOut)
        *equippedItemIdOut = itemId;
    if (equippedSeqOut)
        *equippedSeqOut = seq;
    if (slotOut)
        *slotOut = slot;
    if (oldItemIdOut)
        *oldItemIdOut = oldItemId;
    if (reasonOut)
        *reasonOut = "ok";
    (void)remaining;
    return true;
}

