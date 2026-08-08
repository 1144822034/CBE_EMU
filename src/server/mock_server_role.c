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

static bool vm_mock_mysql_parse_u64(const char *value, size_t value_len,
                                    uint64_t *result_out)
{
    uint64_t result = 0;
    const uint64_t maximum = ~(uint64_t)0;

    if (value == NULL || value_len == 0 || result_out == NULL)
        return false;
    for (size_t i = 0; i < value_len; ++i)
    {
        uint64_t digit = 0;

        if (value[i] < '0' || value[i] > '9')
            return false;
        digit = (uint64_t)(value[i] - '0');
        if (result > (maximum - digit) / 10u)
            return false;
        result = result * 10u + digit;
    }
    *result_out = result;
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
#define VM_MOCK_ACCOUNT_WCOIN_WALLET_MIGRATION "account-wcoin-wallet-v1"

static bool g_vm_mock_mysql_authority_prepared = false;
static bool g_vm_mock_mysql_authority_sealed = false;
/* False only during the one boot where legacy role-bound W coin must still be
 * written long enough for the transactional account-wallet migration below
 * to consume it.  No live client is accepted before that migration finishes. */
static bool g_vm_mock_account_wcoin_wallet_migrated = false;

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
        "account_wallets",
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
    const u8 expected_mask = (u8)((1u << 8) - 1u);

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

    /* W coin is account currency.  The CBE still receives it through its
     * role-shaped actor/shop packets, but this relation is the single durable
     * authority shared by every role under the account. */
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_wallets ("
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "wcoin INT UNSIGNED NOT NULL DEFAULT 0,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(account_id),"
            "CONSTRAINT fk_account_wallets_account FOREIGN KEY(account_id) "
            "REFERENCES accounts(account_id) ON DELETE CASCADE) ENGINE=InnoDB"))
    {
        printf("[error][mock-service] mysql_authority_prepare wallet schema error=%s\n",
               vm_mysql_last_error());
        return false;
    }

    memset(&engine_context, 0, sizeof(engine_context));
    if (!vm_mysql_query(
            "SELECT TABLE_NAME,ENGINE FROM information_schema.TABLES "
            "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN "
            "('accounts','account_wallets','friendships','account_role_state','account_roles',"
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
    vm_mock_service_account_db_file *database;
    bool invalid;
} vm_mock_mysql_account_load_context;

/* The account table is authoritative in MySQL.  The in-process index is only
 * a login/admin cache, so reserve exactly the capacity that the current
 * database needs instead of embedding one million 128-byte records in PE
 * .data.  This also keeps legacy import off the service stack. */
static bool vm_mock_service_account_db_reserve(
    vm_mock_service_account_db_file *database, u32 required)
{
    vm_mock_service_account_record *records;
    u32 capacity;
    size_t oldBytes;
    size_t newBytes;

    if (database == NULL || required > VM_MOCK_SERVICE_ACCOUNT_DB_MAX_ACCOUNTS)
        return false;
    if (required <= database->accountCapacity)
        return true;
    capacity = database->accountCapacity;
    if (capacity == 0)
        capacity = VM_MOCK_SERVICE_ACCOUNT_DB_INITIAL_CAPACITY;
    while (capacity < required)
    {
        u32 growth = capacity / 2;

        if (growth < VM_MOCK_SERVICE_ACCOUNT_DB_INITIAL_CAPACITY)
            growth = VM_MOCK_SERVICE_ACCOUNT_DB_INITIAL_CAPACITY;
        if (capacity > VM_MOCK_SERVICE_ACCOUNT_DB_MAX_ACCOUNTS - growth)
            capacity = VM_MOCK_SERVICE_ACCOUNT_DB_MAX_ACCOUNTS;
        else
            capacity += growth;
    }
    if ((size_t)capacity > (size_t)-1 / sizeof(*records))
        return false;
    oldBytes = (size_t)database->accountCapacity * sizeof(*records);
    newBytes = (size_t)capacity * sizeof(*records);
    records = (vm_mock_service_account_record *)realloc(database->accounts,
                                                         newBytes);
    if (records == NULL)
        return false;
    memset((u8 *)records + oldBytes, 0, newBytes - oldBytes);
    database->accounts = records;
    database->accountCapacity = capacity;
    return true;
}

static void vm_mock_service_account_db_reset(
    vm_mock_service_account_db_file *database)
{
    if (database == NULL)
        return;
    free(database->accounts);
    memset(database, 0, sizeof(*database));
}

static bool vm_mock_mysql_account_row(void *context_value,
                                      unsigned int column_count,
                                      const char *const *values,
                                      const size_t *lengths)
{
    vm_mock_mysql_account_load_context *context = (vm_mock_mysql_account_load_context *)context_value;
    if (context == NULL || context->database == NULL || column_count != 2 ||
        context->database->accountCount >= VM_MOCK_SERVICE_ACCOUNT_DB_MAX_ACCOUNTS ||
        !vm_mock_service_account_db_reserve(
            context->database, context->database->accountCount + 1))
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

static bool vm_mock_service_account_db_load_legacy(
    vm_mock_service_account_db_file *database)
{
    char path[128];
    vm_mock_service_account_db_legacy_header header;
    vm_mock_service_account_db_file loaded;

    if (database == NULL)
        return false;
    vm_mock_service_account_db_path(path, sizeof(path));
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return false;
    memset(&header, 0, sizeof(header));
    memset(&loaded, 0, sizeof(loaded));
    bool valid = fread(&header, 1, sizeof(header), fp) == sizeof(header) &&
                 memcmp(header.magic, "JHA1", 4) == 0 &&
                 header.version == VM_MOCK_SERVICE_ACCOUNT_DB_VERSION &&
                 header.accountCount <= VM_MOCK_SERVICE_ACCOUNT_DB_MAX_ACCOUNTS &&
                 vm_mock_service_account_db_reserve(
                     &loaded, header.accountCount);
    if (valid && header.accountCount != 0)
    {
        valid = fread(loaded.accounts, sizeof(*loaded.accounts),
                      header.accountCount, fp) == header.accountCount;
    }
    fclose(fp);
    if (!valid)
    {
        vm_mock_service_account_db_reset(&loaded);
        return false;
    }
    memcpy(loaded.magic, header.magic, sizeof(loaded.magic));
    loaded.version = header.version;
    loaded.accountCount = header.accountCount;
    vm_mock_service_account_db_reset(database);
    *database = loaded;
    return true;
}

/* Legacy import is the only snapshot operation.  Normal registration and
 * password changes must write just the record that changed. */
static bool vm_mock_service_account_db_save_all(const char *reason)
{
    if (!g_vm_mock_service_account_db_valid)
        return false;
    memcpy(g_vm_mock_service_account_db.magic, "JHA1", 4);
    g_vm_mock_service_account_db.version = VM_MOCK_SERVICE_ACCOUNT_DB_VERSION;
    if (g_vm_mock_service_account_db.accountCount > VM_MOCK_SERVICE_ACCOUNT_DB_MAX_ACCOUNTS)
        g_vm_mock_service_account_db.accountCount = VM_MOCK_SERVICE_ACCOUNT_DB_MAX_ACCOUNTS;

    /*
     * accounts is a parent of persisted recharge orders.  Saving the in-memory
     * legacy snapshot by deleting the whole table breaks that foreign-key
     * contract, so even the import path must preserve existing parent rows.
     */
    if (!vm_mysql_exec("START TRANSACTION"))
    {
        char mysql_error[512];
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        vm_autotest_note("mock_account_db_mysql_save_failed reason=%s error=%s\n",
                         reason ? reason : "state", mysql_error);
        return false;
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
            return false;
        }
        snprintf(query, sizeof(query),
                 "INSERT IGNORE INTO account_wallets(account_id,wcoin) "
                 "VALUES(CAST(X'%s' AS CHAR),0)", username_hex);
        if (!vm_mysql_exec(query))
        {
            vm_autotest_note("mock_account_db_mysql_save_failed reason=%s index=%u wallet_error=%s\n",
                             reason ? reason : "state", i, vm_mysql_last_error());
            vm_mysql_exec("ROLLBACK");
            return false;
        }
    }
    if (!vm_mysql_exec("COMMIT"))
    {
        vm_autotest_note("mock_account_db_mysql_save_failed reason=%s error=%s\n",
                         reason ? reason : "state", vm_mysql_last_error());
        vm_mysql_exec("ROLLBACK");
        return false;
    }
    vm_autotest_note("mock_account_db_mysql_save reason=%s count=%u\n",
                     reason ? reason : "state",
                     g_vm_mock_service_account_db.accountCount);
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
        if (!vm_mysql_exec("START TRANSACTION"))
        {
            vm_autotest_note("mock_account_db_mysql_write_failed reason=%s operation=insert error=%s\n",
                             reason ? reason : "state", vm_mysql_last_error());
            return false;
        }
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
        if (create)
            (void)vm_mysql_exec("ROLLBACK");
        return false;
    }
    if (create)
    {
        snprintf(query, sizeof(query),
                 "INSERT INTO account_wallets(account_id,wcoin) "
                 "VALUES(CAST(X'%s' AS CHAR),0)", username_hex);
        if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        {
            vm_autotest_note("mock_account_db_mysql_write_failed reason=%s operation=insert wallet_error=%s\n",
                             reason ? reason : "state", vm_mysql_last_error());
            (void)vm_mysql_exec("ROLLBACK");
            return false;
        }
    }
    vm_autotest_note("mock_account_db_mysql_write reason=%s operation=%s\n",
                     reason ? reason : "state", create ? "insert" : "update");
    return true;
}

/* The normal runtime directory is an online-account cache.  It must never
 * become a second authority for the complete accounts table: a cold service
 * starts empty and individual game logins explicitly acquire their record.
 * The one-time legacy migration below is the only operation allowed to load
 * every account, because it has to open each old role payload before the
 * MySQL-authority marker is sealed. */
static void vm_mock_service_account_db_load(void)
{
    if (g_vm_mock_service_account_db_loaded)
        return;
    g_vm_mock_service_account_db_loaded = true;
    vm_mock_service_account_db_reset(&g_vm_mock_service_account_db);
    memcpy(g_vm_mock_service_account_db.magic, "JHA1", 4);
    g_vm_mock_service_account_db.version = VM_MOCK_SERVICE_ACCOUNT_DB_VERSION;
    g_vm_mock_service_account_db_valid = true;

    vm_autotest_note("mock_account_cache_init entries=0 source=runtime-lifecycle\n");
}

static bool vm_mock_service_account_db_load_all_for_legacy_migration(void)
{
    vm_mock_service_account_db_file loaded;
    vm_mock_mysql_account_load_context context;
    bool has_role_data = false;

    vm_mock_service_account_db_load();
    if (!g_vm_mock_service_account_db_valid)
        return false;
    if (vm_mock_service_mysql_authority_is_sealed())
        return true;

    memset(&loaded, 0, sizeof(loaded));
    memcpy(loaded.magic, "JHA1", 4);
    loaded.version = VM_MOCK_SERVICE_ACCOUNT_DB_VERSION;
    memset(&context, 0, sizeof(context));
    context.database = &loaded;
    if (!vm_mysql_query("SELECT account_id,HEX(password_value) FROM accounts ORDER BY account_id",
                        vm_mock_mysql_account_row, &context))
    {
        vm_mock_service_account_db_reset(&loaded);
        vm_autotest_note("mock_account_db_legacy_load_failed error=%s\n", vm_mysql_last_error());
        return false;
    }
    if (context.invalid)
    {
        vm_mock_service_account_db_reset(&loaded);
        vm_autotest_note("mock_account_db_legacy_load_failed error=invalid-row\n");
        return false;
    }
    if (loaded.accountCount == 0)
    {
        /* An empty account table alongside role rows/payload backups is an
         * integrity failure, not a fresh installation.  Importing the old
         * file at this point was the path that made a later process restart
         * appear to restore an account list from the distant past. */
        if (!vm_mock_service_mysql_has_role_data(&has_role_data))
        {
            vm_mock_service_account_db_reset(&loaded);
            vm_autotest_note("mock_account_db_legacy_load_failed error=authority-probe:%s\n",
                             vm_mysql_last_error());
            return false;
        }
        if (has_role_data)
        {
            vm_mock_service_account_db_reset(&loaded);
            vm_autotest_note("mock_account_db_legacy_load_failed error=empty-accounts-with-role-data\n");
            return false;
        }
        if (vm_mock_service_account_db_load_legacy(&loaded))
        {
            vm_autotest_note("mock_account_db_legacy_import count=%u\n",
                             loaded.accountCount);
        }
    }
    vm_mock_service_account_db_reset(&g_vm_mock_service_account_db);
    g_vm_mock_service_account_db = loaded;
    g_vm_mock_service_account_db_valid = true;
    if (g_vm_mock_service_account_db.accountCount != 0 &&
        !vm_mock_service_account_db_save_all("legacy-migrate"))
    {
        g_vm_mock_service_account_db_valid = false;
        return false;
    }
    vm_autotest_note("mock_account_db_legacy_load count=%u\n",
                     g_vm_mock_service_account_db.accountCount);
    return true;
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

    if (!g_vm_mock_service_friend_db_valid || forward == NULL || reverse == NULL)
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
    if (loaded.recordCount == 0 && !vm_mock_service_mysql_authority_is_sealed())
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
    vm_mock_service_friend_db_file candidate;
    const vm_mock_service_friend_record *forwardRecord;
    const vm_mock_service_friend_record *reverseRecord;

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
    candidate = g_vm_mock_service_friend_db;
    if (!vm_mock_service_friend_db_upsert_one(&candidate, ownerAccountId, ownerRoleId,
                                               targetAccountId, targetRoleId,
                                               targetRoleName, targetLevel, targetJob, targetSex,
                                               &forwardCreated, &forwardChanged) ||
        !vm_mock_service_friend_db_upsert_one(&candidate, targetAccountId, targetRoleId,
                                               ownerAccountId, ownerRoleId,
                                               ownerRoleName, ownerLevel, ownerJob, ownerSex,
                                               &reverseCreated, &reverseChanged))
    {
        return false;
    }
    if (forwardChanged || reverseChanged)
    {
        forwardRecord = vm_mock_service_friend_db_find_in(&candidate,
                                                           ownerAccountId, ownerRoleId,
                                                           targetAccountId, targetRoleId);
        reverseRecord = vm_mock_service_friend_db_find_in(&candidate,
                                                           targetAccountId, targetRoleId,
                                                           ownerAccountId, ownerRoleId);
        if (forwardRecord == NULL || reverseRecord == NULL ||
            !vm_mock_service_friend_db_write_pair(forwardRecord, reverseRecord,
                                                  "friend-invite-accepted"))
        {
            printf("[error][mock-service] friend_pair_persist_failed owner=%s/%u target=%s/%u error=%s\n",
                   ownerAccountId, ownerRoleId, targetAccountId, targetRoleId,
                   vm_mysql_last_error());
            return false;
        }
    }
    g_vm_mock_service_friend_db = candidate;
    if (createdOut)
        *createdOut = forwardCreated || reverseCreated;
    return true;
}

typedef struct
{
    vm_mock_service_account_record record;
    bool found;
    bool invalid;
} vm_mock_mysql_account_lookup_context;

static bool vm_mock_mysql_account_lookup_row(void *context_value,
                                             unsigned int column_count,
                                             const char *const *values,
                                             const size_t *lengths)
{
    vm_mock_mysql_account_lookup_context *context =
        (vm_mock_mysql_account_lookup_context *)context_value;
    size_t password_len = 0;

    if (context == NULL || context->found || column_count != 2 ||
        !vm_mock_mysql_copy_text(context->record.username,
                                 sizeof(context->record.username),
                                 values[0], lengths[0]) || values[1] == NULL ||
        !vm_mysql_hex_decode(values[1], lengths[1], context->record.password,
                             sizeof(context->record.password) - 1,
                             &password_len))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->record.password[password_len] = 0;
    context->found = true;
    return true;
}

/* This is deliberately an exact MySQL lookup rather than a cache miss that
 * silently repopulates all accounts.  Authentication, web sessions and
 * administration only need one record at a time; the runtime cache belongs
 * exclusively to an active game session. */
static bool vm_mock_service_account_query_record(
    const char *username, vm_mock_service_account_record *recordOut,
    bool *foundOut)
{
    vm_mock_mysql_account_lookup_context context;
    char username_hex[128];
    char query[384];
    size_t username_len = 0;

    if (foundOut != NULL)
        *foundOut = false;
    if (recordOut != NULL)
        memset(recordOut, 0, sizeof(*recordOut));
    vm_mock_service_account_db_load();
    if (!g_vm_mock_service_account_db_valid || username == NULL)
        return false;
    username_len = vm_mock_mysql_bounded_strlen(username, 64);
    if (username_len == 0 || username_len >= 64 ||
        vm_mysql_hex_encode(username, username_len, username_hex,
                            sizeof(username_hex)) == 0)
    {
        return false;
    }
    snprintf(query, sizeof(query),
             "SELECT account_id,HEX(password_value) FROM accounts "
             "WHERE account_id=CAST(X'%s' AS CHAR) LIMIT 1",
             username_hex);
    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(query, vm_mock_mysql_account_lookup_row, &context) ||
        context.invalid)
    {
        vm_autotest_note("mock_account_lookup_failed account=%s error=%s\n",
                         username, vm_mysql_last_error());
        return false;
    }
    if (context.found && recordOut != NULL)
        *recordOut = context.record;
    if (foundOut != NULL)
        *foundOut = context.found;
    return true;
}

static bool vm_mock_service_account_exists(const char *username)
{
    bool found = false;

    return vm_mock_service_account_query_record(username, NULL, &found) && found;
}

/* Cache-only lookup: callers that need arbitrary accounts must use the exact
 * database query above.  This keeps cache size proportional to live game
 * sessions rather than account history or admin-page visits. */
static vm_mock_service_account_record *vm_mock_service_account_find_record(const char *username)
{
    if (!g_vm_mock_service_account_db_valid || username == NULL || username[0] == 0)
        return NULL;
    for (u32 i = 0; i < g_vm_mock_service_account_db.accountCount; ++i)
    {
        if (strcmp(g_vm_mock_service_account_db.accounts[i].username, username) == 0)
            return &g_vm_mock_service_account_db.accounts[i];
    }
    return NULL;
}

static bool vm_mock_service_account_cache_acquire(const char *username,
                                                  const char *reason)
{
    vm_mock_service_account_record record;
    vm_mock_service_account_record *cached = NULL;
    bool found = false;

    vm_mock_service_account_db_load();
    if (!g_vm_mock_service_account_db_valid || username == NULL || username[0] == 0)
        return false;
    cached = vm_mock_service_account_find_record(username);
    if (cached != NULL)
        return true;
    if (!vm_mock_service_account_query_record(username, &record, &found) || !found)
        return false;
    if (!vm_mock_service_account_db_reserve(
            &g_vm_mock_service_account_db,
            g_vm_mock_service_account_db.accountCount + 1))
    {
        return false;
    }
    g_vm_mock_service_account_db.accounts[
        g_vm_mock_service_account_db.accountCount++] = record;
    printf("[info][mock-service] account_cache_acquire account=%s entries=%u reason=%s\n",
           username, g_vm_mock_service_account_db.accountCount,
           reason ? reason : "-");
    return true;
}

static void vm_mock_service_account_cache_release(const char *username,
                                                  const char *reason)
{
    u32 index = 0;

    if (!g_vm_mock_service_account_db_valid || username == NULL || username[0] == 0)
        return;
    while (index < g_vm_mock_service_account_db.accountCount &&
           strcmp(g_vm_mock_service_account_db.accounts[index].username,
                  username) != 0)
    {
        ++index;
    }
    if (index == g_vm_mock_service_account_db.accountCount)
        return;
    if (index + 1 < g_vm_mock_service_account_db.accountCount)
    {
        memmove(&g_vm_mock_service_account_db.accounts[index],
                &g_vm_mock_service_account_db.accounts[index + 1],
                (g_vm_mock_service_account_db.accountCount - index - 1) *
                    sizeof(g_vm_mock_service_account_db.accounts[0]));
    }
    --g_vm_mock_service_account_db.accountCount;
    memset(&g_vm_mock_service_account_db.accounts[
               g_vm_mock_service_account_db.accountCount],
           0, sizeof(g_vm_mock_service_account_db.accounts[0]));
    if (g_vm_mock_service_account_db.accountCount == 0)
    {
        free(g_vm_mock_service_account_db.accounts);
        g_vm_mock_service_account_db.accounts = NULL;
        g_vm_mock_service_account_db.accountCapacity = 0;
    }
    printf("[info][mock-service] account_cache_release account=%s entries=%u reason=%s\n",
           username, g_vm_mock_service_account_db.accountCount,
           reason ? reason : "-");
}

static void vm_mock_service_account_cache_clear(const char *reason)
{
    u32 released = g_vm_mock_service_account_db.accountCount;

    if (!g_vm_mock_service_account_db_valid)
        return;
    free(g_vm_mock_service_account_db.accounts);
    g_vm_mock_service_account_db.accounts = NULL;
    g_vm_mock_service_account_db.accountCount = 0;
    g_vm_mock_service_account_db.accountCapacity = 0;
    if (released != 0)
    {
        printf("[info][mock-service] account_cache_clear released=%u reason=%s\n",
               released, reason ? reason : "-");
    }
}

static bool vm_mock_service_account_verify_credentials(const char *username, const char *password)
{
    vm_mock_service_account_record record;
    bool found = false;

    if (password == NULL ||
        !vm_mock_service_account_query_record(username, &record, &found) ||
        !found)
    {
        return false;
    }
    return strcmp(record.password, password) == 0;
}

static bool vm_mock_service_account_copy_password(const char *username,
                                                  char *passwordOut,
                                                  size_t passwordOutCap)
{
    vm_mock_service_account_record record;
    bool found = false;

    if (passwordOut == NULL || passwordOutCap == 0)
        return false;
    passwordOut[0] = 0;
    if (!vm_mock_service_account_query_record(username, &record, &found) ||
        !found)
    {
        return false;
    }
    snprintf(passwordOut, passwordOutCap, "%s", record.password);
    return passwordOut[0] != 0;
}

static bool vm_mock_service_account_create_record(const char *username,
                                                  const char *password,
                                                  const char **messageOut)
{
    bool found = false;

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
    if (!vm_mock_service_account_query_record(username, NULL, &found))
    {
        if (messageOut)
            *messageOut = "account db unavailable";
        return false;
    }
    if (found)
    {
        if (messageOut)
            *messageOut = "account already exists";
        return false;
    }
    vm_mock_service_account_record pending;
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

static bool vm_mock_service_account_set_password(const char *username,
                                                 const char *password,
                                                 const char **messageOut)
{
    vm_mock_service_account_record pending;
    vm_mock_service_account_record *cached = NULL;
    bool found = false;

    if (messageOut)
        *messageOut = "ok";
    if (!vm_mock_service_account_query_record(username, &pending, &found))
    {
        if (messageOut)
            *messageOut = "account db unavailable";
        return false;
    }
    if (!found)
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
    snprintf(pending.password, sizeof(pending.password), "%s", password);
    if (!vm_mock_service_account_db_write_record(&pending, false, "passwd"))
    {
        if (messageOut)
            *messageOut = "account persistence failed";
        return false;
    }
    cached = vm_mock_service_account_find_record(username);
    if (cached != NULL)
        *cached = pending;
    return true;
}

static bool vm_mock_mysql_account_guest_ordinal_row(void *context_value,
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

static bool vm_mock_service_account_guest_next_ordinal(u32 *ordinalOut)
{
    vm_mock_mysql_authority_count_context context;

    if (ordinalOut != NULL)
        *ordinalOut = 0;
    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(
            "SELECT COALESCE(MAX(CAST(SUBSTRING(account_id,6) AS UNSIGNED)),0) "
            "FROM accounts WHERE account_id REGEXP '^guest[0-9]+$'",
            vm_mock_mysql_account_guest_ordinal_row, &context) ||
        context.invalid || !context.found || context.value == 0xffffffffu)
    {
        return false;
    }
    if (ordinalOut != NULL)
        *ordinalOut = context.value + 1;
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

    if (!vm_mock_service_account_guest_next_ordinal(&seedBase))
        return false;
    for (u32 attempt = 0; attempt < 100000; ++attempt)
    {
        u32 ordinal = seedBase + attempt;
        if (ordinal < seedBase)
            break;
        snprintf(usernameOut, usernameOutCap, "guest%05u", ordinal);
        snprintf(passwordOut, passwordOutCap, "g%08X", clientId ^ (ordinal * 2654435761u));
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

/* EXP is persisted as a cumulative total.  The table is therefore the total
 * EXP at which the indexed level starts, rather than a client-side display
 * table.  Its adjacent differences form a deterministic exponential curve:
 * reaching level 49 costs exactly 2,000,000 EXP and reaching the level-70 cap
 * costs exactly 200,000,000 EXP.  Keeping the values literal avoids a
 * platform-dependent floating-point calculation during login or settlement. */
static const u32 g_vm_net_mock_role_level_start_exp[VM_NET_MOCK_ROLE_LEVEL_CAP + 1] = {
    0, 0, 67, 150, 254, 383, 544, 744, 993, 1303,
    1689, 2170, 2769, 3515, 4443, 5599, 7038, 8830, 11062, 13841,
    17301, 21610, 26975, 33656, 41975, 52334, 65233, 81295, 101295, 126199,
    157209, 195823, 243905, 303777, 378329, 471161, 586755, 730692, 909922, 1133099,
    1410998, 1757037, 2187924, 2724463, 3392560, 4224472, 5260367, 6550260, 8156431, 10156431,
    12646825, 15747857, 19609252, 24417450, 30404605, 37859792, 47142970, 58702356, 73096069, 91019079,
    113336759, 141126669, 175730584, 218819278, 272473194, 339282894, 422474137, 526063631, 655052966, 815670110,
    1015670110
};

static u32 vm_net_mock_role_exp_cap(void)
{
    return g_vm_net_mock_role_level_start_exp[VM_NET_MOCK_ROLE_LEVEL_CAP];
}

static u32 vm_net_mock_role_level_start_exp(u32 level)
{
    if (level <= 1)
        return 0;
    if (level > VM_NET_MOCK_ROLE_LEVEL_CAP)
        return 0xffffffffu;
    return g_vm_net_mock_role_level_start_exp[level];
}

static u32 vm_net_mock_role_level_from_exp(u32 exp)
{
    u32 level = 1;

    for (u32 nextLevel = 2; nextLevel <= VM_NET_MOCK_ROLE_LEVEL_CAP; ++nextLevel)
    {
        if (exp < g_vm_net_mock_role_level_start_exp[nextLevel])
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

    /* At the level cap there is no next interval.  The title's EXP widgets
     * receive an equal start/end threshold plus 100 percent, rather than a
     * synthetic level 71 or an overflowing unsigned value. */
    if (level >= VM_NET_MOCK_ROLE_LEVEL_CAP)
        return vm_net_mock_role_exp_cap();
    return vm_net_mock_role_level_start_exp(level + 1);
}

static u32 vm_net_mock_role_exp_percent(u32 exp)
{
    u32 level = vm_net_mock_role_level_from_exp(exp);
    u32 levelStart = vm_net_mock_role_level_start_exp(level);
    u32 nextLevelStart = vm_net_mock_role_next_level_start_exp(exp);
    u32 levelSize = 0;
    u32 current = 0;

    if (level >= VM_NET_MOCK_ROLE_LEVEL_CAP)
        return 100;
    if (exp <= levelStart)
        return 0;
    if (nextLevelStart <= levelStart || nextLevelStart == 0xffffffffu)
        return 100;

    current = exp - levelStart;
    levelSize = nextLevelStart - levelStart;
    return (u32)(((unsigned long long)current * 100ull) / levelSize);
}

/* Version-6 and earlier role rows used a linear interval: reaching level N
 * cost 100 * (N - 1).  Retain that decoder only for the one-time database/file
 * migration below; live gameplay must exclusively use the capped curve. */
static u32 vm_net_mock_role_legacy_level_start_exp(u32 level)
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
    if (startExp > 0xffffffffull / 100ull)
        return 0xffffffffu;
    return (u32)(startExp * 100ull);
}

static u32 vm_net_mock_role_legacy_level_from_exp(u32 exp)
{
    u32 level = 1;

    for (;;)
    {
        u32 nextLevel = level + 1;
        u32 nextStart = vm_net_mock_role_legacy_level_start_exp(nextLevel);

        if (nextLevel == 0 || nextStart == 0xffffffffu || exp < nextStart)
            break;
        level = nextLevel;
    }
    return level;
}

static bool vm_net_mock_role_migrate_exp_curve(vm_net_mock_role_state *role,
                                                u32 *oldLevelOut)
{
    u32 oldExp = 0;
    u32 oldLevel = 1;
    u32 newLevel = 1;
    u32 newExp = 0;

    if (oldLevelOut)
        *oldLevelOut = 0;
    if (role == NULL)
        return false;
    oldExp = role->exp;
    /* The persisted EXP field is cumulative and is the authoritative measure
     * of progression.  The old level is diagnostics only: preserving it and
     * mapping an old per-level percentage makes a level-162/1.3M-EXP role
     * become level 70 even though 1.3M total EXP is far below the new cap. */
    oldLevel = role->level;
    if (oldLevel == 0)
        oldLevel = vm_net_mock_role_legacy_level_from_exp(oldExp);
    if (oldLevelOut)
        *oldLevelOut = oldLevel;
    newExp = oldExp;
    if (newExp > vm_net_mock_role_exp_cap())
        newExp = vm_net_mock_role_exp_cap();
    newLevel = vm_net_mock_role_level_from_exp(newExp);
    role->exp = newExp;
    role->level = newLevel;
    return oldExp != newExp || oldLevel != newLevel;
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
    u16 minLevel;
    const char *name;
    const char *description;
    const char *overheadResource;
} vm_net_mock_designation_entry;

static const vm_net_mock_designation_entry g_vm_net_mock_designation_entries[] = {
    {
        0,
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
        0,
        "\xd2\xc2\xca\xb3\xce\xde\xd3\xc7", /* GBK: yi shi wu you */
        "\xc2\xd4\xd3\xd0\xbb\xfd\xd0\xee", /* GBK: lue you ji xu */
        "riches_name1.gif",
    },
    {
        2,
        0,
        20000,
        0,
        "\xc9\xfa\xb2\xc6\xd3\xd0\xb5\xc0", /* GBK: sheng cai you dao */
        "\xd0\xa1\xd3\xd0\xd7\xca\xb2\xfa", /* GBK: xiao you zi chan */
        "riches_name2.gif",
    },
    {
        3,
        0,
        50000,
        0,
        "\xc0\xed\xb2\xc6\xd3\xd0\xb7\xbd", /* GBK: li cai you fang */
        "\xb2\xc6\xc2\xb7\xbd\xa5\xbf\xed", /* GBK: cai lu jian kuan */
        "riches_name3.gif",
    },
    {
        4,
        0,
        100000,
        0,
        "\xb2\xc6\xd4\xcb\xba\xe0\xcd\xa8", /* GBK: cai yun heng tong */
        "\xc7\xae\xb2\xc6\xb7\xe1\xba\xf1", /* GBK: qian cai feng hou */
        "riches_name4.gif",
    },
    {
        5,
        0,
        300000,
        0,
        "\xd1\xfc\xb2\xf8\xcd\xf2\xb9\xe1", /* GBK: yao chan wan guan */
        "\xbb\xd3\xbd\xf0\xd3\xd0\xb6\xc8", /* GBK: hui jin you du */
        "riches_name5.gif",
    },
    {
        6,
        0,
        500000,
        0,
        "\xbc\xd2\xb2\xc6\xcd\xf2\xb9\xe1", /* GBK: jia cai wan guan */
        "\xb2\xc6\xb8\xbb\xbe\xaa\xc8\xcb", /* GBK: cai fu jing ren */
        "riches_name6.gif",
    },
    {
        7,
        0,
        1000000,
        0,
        "\xb8\xbb\xc9\xcc\xbe\xde\xbc\xd6", /* GBK: fu shang ju gu */
        "\xc9\xcc\xbc\xd6\xce\xc5\xc3\xfb", /* GBK: shang gu wen ming */
        "riches_name7.gif",
    },
    {
        8,
        0,
        3000000,
        0,
        "\xb8\xbb\xbc\xd7\xd2\xbb\xb7\xbd", /* GBK: fu jia yi fang */
        "\xb2\xc6\xb9\xda\xd2\xbb\xb7\xbd", /* GBK: cai guan yi fang */
        "riches_name8.gif",
    },
    {
        9,
        0,
        10000000,
        0,
        "\xb8\xbb\xbf\xc9\xb5\xd0\xb9\xfa", /* GBK: fu ke di guo */
        "\xcc\xec\xcf\xc2\xbe\xde\xb8\xbb", /* GBK: tian xia ju fu */
        "riches_name9.gif",
    },
    /*
     * The level-title badge strip and individual badge files are present in
     * JHOnlineData as level_name.gif and level_name0.gif..level_name12.gif.
     * Their ids deliberately live outside 0..9 so persisted wealth-title
     * choices remain stable and WT 23/3 can distinguish the two families.
     */
    {
        16, 0, 0, 1,
        "\xB2\xBB\xBF\xB0\xD2\xBB\xBB\xF7", /* GBK: bu kan yi ji */
        "\xB5\xC8\xBC\xB6\xB4\xEF\xB5\xBD" "1" "\xBC\xB6", /* level 1 */
        "level_name0.gif",
    },
    {
        17, 0, 0, 5,
        "\xB3\xF5\xD1\xA7\xD5\xA7\xC1\xB7", /* GBK: chu xue zha lian */
        "\xB5\xC8\xBC\xB6\xB4\xEF\xB5\xBD" "5" "\xBC\xB6", /* level 5 */
        "level_name1.gif",
    },
    {
        18, 0, 0, 10,
        "\xD0\xA1\xCA\xD4\xC5\xA3\xB5\xB6", /* GBK: xiao shi niu dao */
        "\xB5\xC8\xBC\xB6\xB4\xEF\xB5\xBD" "10" "\xBC\xB6", /* level 10 */
        "level_name2.gif",
    },
    {
        19, 0, 0, 15,
        "\xB3\xF5\xC2\xB6\xB7\xE6\xC3\xA2", /* GBK: chu lu feng mang */
        "\xB5\xC8\xBC\xB6\xB4\xEF\xB5\xBD" "15" "\xBC\xB6", /* level 15 */
        "level_name3.gif",
    },
    {
        20, 0, 0, 20,
        "\xB3\xF6\xC8\xCB\xCD\xB7\xB5\xD8", /* GBK: chu ren tou di */
        "\xB5\xC8\xBC\xB6\xB4\xEF\xB5\xBD" "20" "\xBC\xB6", /* level 20 */
        "level_name4.gif",
    },
    {
        21, 0, 0, 25,
        "\xC3\xFB\xD5\xF0\xBD\xAD\xBA\xFE", /* GBK: ming zhen jiang hu */
        "\xB5\xC8\xBC\xB6\xB4\xEF\xB5\xBD" "25" "\xBC\xB6", /* level 25 */
        "level_name5.gif",
    },
    {
        22, 0, 0, 30,
        "\xBD\xAD\xBA\xFE\xBA\xC0\xBD\xDC", /* GBK: jiang hu hao jie */
        "\xB5\xC8\xBC\xB6\xB4\xEF\xB5\xBD" "30" "\xBC\xB6", /* level 30 */
        "level_name6.gif",
    },
    {
        23, 0, 0, 35,
        "\xC1\xCB\xC8\xBB\xD3\xDA\xD0\xD8", /* GBK: liao ran yu xiong */
        "\xB5\xC8\xBC\xB6\xB4\xEF\xB5\xBD" "35" "\xBC\xB6", /* level 35 */
        "level_name7.gif",
    },
    {
        24, 0, 0, 40,
        "\xC2\xAF\xBB\xF0\xB4\xBF\xC7\xE0", /* GBK: lu huo chun qing */
        "\xB5\xC8\xBC\xB6\xB4\xEF\xB5\xBD" "40" "\xBC\xB6", /* level 40 */
        "level_name8.gif",
    },
    {
        25, 0, 0, 45,
        "\xBD\xAD\xBA\xFE\xCF\xC0\xD2\xFE", /* GBK: jiang hu xia yin */
        "\xB5\xC8\xBC\xB6\xB4\xEF\xB5\xBD" "45" "\xBC\xB6", /* level 45 */
        "level_name9.gif",
    },
    {
        26, 0, 0, 50,
        "\xB5\xC7\xB7\xE5\xD4\xEC\xBC\xAB", /* GBK: deng feng zao ji */
        "\xB5\xC8\xBC\xB6\xB4\xEF\xB5\xBD" "50" "\xBC\xB6", /* level 50 */
        "level_name10.gif",
    },
    {
        27, 0, 0, 55,
        "\xB3\xAC\xD4\xBD\xBC\xAB\xCF\xDE", /* GBK: chao yue ji xian */
        "\xB5\xC8\xBC\xB6\xB4\xEF\xB5\xBD" "55" "\xBC\xB6", /* level 55 */
        "level_name11.gif",
    },
    {
        28, 0, 0, 60,
        "\xBF\xAA\xC9\xBD\xB1\xC7\xD7\xE6", /* GBK: kai shan bi zu */
        "\xB5\xC8\xBC\xB6\xB4\xEF\xB5\xBD" "60" "\xBC\xB6", /* level 60 */
        "level_name12.gif",
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
    return NULL;
}

static bool vm_net_mock_designation_is_unlocked(const vm_net_mock_role_state *role,
                                                const vm_net_mock_designation_entry *entry)
{
    u32 money = role ? role->money : VM_NET_MOCK_ROLE_DEFAULT_MONEY;
    u32 level = role ? role->level : 1;
    if (entry == NULL)
        return false;
    return money >= entry->minMoney && level >= entry->minLevel;
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
    if (entry != NULL && vm_net_mock_designation_is_unlocked(role, entry))
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
        /* JianghuOL.CBE: scene_apply_levelup_status_growth (0x01017F1C)
         * proves the first three per-level growth values for visualVariant
         * 0..2 (login job 1..3): strength, agility, wisdom.  The last two
         * legacy mock values remain intentionally unchanged until their
         * ActorInfo ownership/growth has matching client or server evidence. */
        {12, 4, 6, 3, 1},
        {6, 10, 3, 2, 1},
        {3, 6, 12, 2, 1},
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

/* JianghuOL.CBE: scene_apply_levelup_status_growth (0x01017F1C) changes
 * ActorSceneNode::statusBarPrimaryBaseMax / statusBarSecondaryBaseMax by
 * these exact values for visualVariant 0, 1 and 2.  Login maps job 1..3 to
 * that same zero-based visualVariant, so the server's durable base maxima
 * must use this progression as well.  Equipment HP/MP are deliberately not
 * part of this helper: the scene client adds them from 1/7/7 after ActorInfo.
 */
static u32 vm_net_mock_role_base_vital_max(u32 level, u32 job, bool primary)
{
    static const u16 primary_growth[3] = {25, 20, 15};
    static const u16 secondary_growth[3] = {15, 20, 25};
    u32 job_index = (job == 0 || job > 3) ? 0 : job - 1;
    u32 base = primary ? VM_NET_MOCK_ROLE_DEFAULT_HP : VM_NET_MOCK_ROLE_DEFAULT_MP;
    u32 growth = primary ? primary_growth[job_index] : secondary_growth[job_index];

    if (level == 0)
        level = 1;
    return base + (level - 1) * growth;
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
        const vm_net_mock_equipped_item_state *equipped =
            &role->equippedItems[slot];
        u32 itemId = equipped->itemId;
        const vm_net_mock_equipment_catalog_item *item = NULL;

        /* scene_rebuild_status_meter_node only applies an equipment record
         * while its client-side current durability is positive.  Battle uses
         * the same durable instance, so a broken item cannot remain a hidden
         * server-only stat bonus. */
        if (itemId == 0 || equipped->durability == 0)
            continue;
        item = vm_net_mock_find_equipment_catalog_item(itemId);
        if (item == NULL || item->slot != slot)
            continue;
        if (item->levelRequired > level)
            continue;
        vm_net_mock_equipment_bonus_add(bonus, &item->bonus);
        /* Match scene_rebuild_status_meter_node: the direct equip.dsh fields
         * apply first, then the per-level primary growth and any unlocked
         * +4/+8/+12/+16 attribute rows. */
        vm_net_mock_equipment_enhancement_add_bonus(
            item,
            (u8)SDL_min(equipped->enhanceLevel,
                        VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL),
            bonus);
    }
}

static void vm_net_mock_role_build_player_stats_impl(
    const vm_net_mock_role_state *role,
    vm_net_mock_player_stats *stats,
    bool include_equipment)
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

    if (include_equipment)
        vm_net_mock_role_collect_equipment_bonus(role, level, &equipment);
    else
        memset(&equipment, 0, sizeof(equipment));
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

    /* The old 90 + level*8 + endurance*2 / 70 + level*9 + wisdom*3 model
     * was an emulator-only approximation.  Unlike attack and defense, the
     * client has an exact, class-specific level growth path for HP/MP; use it
     * here so durable battle maxima and the client-side reconstructed meter
     * start from the same base. */
    stats->maxHp = vm_net_mock_role_base_vital_max(level, job, true) + equipment.hp;
    stats->maxMp = vm_net_mock_role_base_vital_max(level, job, false) + equipment.mp;
    /* The two base combat columns in equip.dsh are not balancing hints.  The
     * client loads them into an equipped item and
     * CalcEquipStatBonus(0x01028B34) first adds the exact base value, then
     * applies enhancement-specific percentage additions.  Dividing attack by
     * 3 or armor by 5 here was an old mock-only curve and made a correctly
     * displayed item contribute a different server-side base value.  Keep the
     * still-unrecovered role-derived part separate, but carry each confirmed
     * DSH equipment value at full strength into authoritative combat. */
    stats->attack = 6 + level * 2 + stats->strength / 2 + equipment.attack;
    stats->defense = 4 + level + stats->endurance / 2 + equipment.armor;
    stats->hit = 75 + level + stats->agility * 2 + equipment.hit;
    /* `scene_rebuild_status_meter_node(0x0100FED8)` passes the DSH combat
     * fields to AddActorStatBonus(0x0100FE2C), which adds each supplied field
     * directly to the matching client status slot.  As with attack/armor,
     * the old `/2` factors below were only mock balancing and gave a durable
     * equipped instance a different authoritative base from its client-side
     * stat contribution. */
    stats->dodge = 3 + level / 2 + stats->agility / 2 + equipment.dodge;
    stats->crit = 1 + stats->agility / 3 + stats->wisdom / 5 + equipment.crit;
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

/* ActorInfo is the base-status layer.  JianghuOL.CBE then rebuilds the status
 * meter from its locally parsed 1/7/7 equipment records, so this path must not
 * include gear a second time.  Server combat calls the full variant below. */
static void vm_net_mock_role_build_base_player_stats(
    const vm_net_mock_role_state *role, vm_net_mock_player_stats *stats)
{
    vm_net_mock_role_build_player_stats_impl(role, stats, false);
}

static void vm_net_mock_role_build_player_stats(const vm_net_mock_role_state *role,
                                                vm_net_mock_player_stats *stats)
{
    vm_net_mock_role_build_player_stats_impl(role, stats, true);
}

static u32 vm_net_mock_battle_apply_signed_stat_change(u32 value, int32_t change)
{
    if (change >= 0)
    {
        uint64_t raised = (uint64_t)value + (uint32_t)change;
        return raised > 9999u ? 9999u : (u32)raised;
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
    u32 strengthBefore = 0;
    u32 agilityBefore = 0;
    u32 wisdomBefore = 0;

    if (stats == NULL || modifier->remainingRounds == 0)
        return;
    strengthBefore = stats->strength;
    agilityBefore = stats->agility;
    wisdomBefore = stats->wisdom;
    stats->strength = vm_net_mock_battle_apply_signed_stat_change(
        stats->strength, modifier->strength);
    stats->agility = vm_net_mock_battle_apply_signed_stat_change(
        stats->agility, modifier->agility);
    stats->wisdom = vm_net_mock_battle_apply_signed_stat_change(
        stats->wisdom, modifier->wisdom);
    /* The derived combat values were originally calculated from the unbuffed
     * primary attributes.  Apply their exact formula deltas before any direct
     * skill.dsh combat-stat deltas, so `力量+60` really changes attack rather
     * than merely changing an otherwise unused display field. */
    stats->attack = vm_net_mock_battle_apply_signed_stat_change(
        stats->attack, (int32_t)(stats->strength / 2) -
                      (int32_t)(strengthBefore / 2));
    stats->hit = vm_net_mock_battle_apply_signed_stat_change(
        stats->hit, ((int32_t)stats->agility - (int32_t)agilityBefore) * 2);
    stats->dodge = vm_net_mock_battle_apply_signed_stat_change(
        stats->dodge, (int32_t)(stats->agility / 2) -
                      (int32_t)(agilityBefore / 2));
    stats->crit = vm_net_mock_battle_apply_signed_stat_change(
        stats->crit, (int32_t)(stats->agility / 3) -
                     (int32_t)(agilityBefore / 3) +
                     (int32_t)(stats->wisdom / 5) -
                     (int32_t)(wisdomBefore / 5));
    stats->resist = vm_net_mock_battle_apply_signed_stat_change(
        stats->resist, (int32_t)(stats->wisdom / 2) -
                       (int32_t)(wisdomBefore / 2));
    stats->attack = vm_net_mock_battle_apply_signed_stat_change(
        stats->attack, modifier->attack);
    stats->defense = vm_net_mock_battle_apply_signed_stat_change(
        stats->defense, modifier->defense);
    stats->crit = vm_net_mock_battle_apply_signed_stat_change(
        stats->crit, modifier->crit);
    stats->hit = vm_net_mock_battle_apply_signed_stat_change(
        stats->hit, modifier->hit);
    stats->dodge = vm_net_mock_battle_apply_signed_stat_change(
        stats->dodge, modifier->dodge);
    stats->resist = vm_net_mock_battle_apply_signed_stat_change(
        stats->resist, modifier->resist);
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
    u32 expCap = vm_net_mock_role_exp_cap();
    u32 acceptedExp = 0;

    if (role == NULL || addExp == 0)
        return false;

    oldLevel = vm_net_mock_role_level_from_exp(role->exp);
    if (role->exp >= expCap)
    {
        role->exp = expCap;
        role->level = VM_NET_MOCK_ROLE_LEVEL_CAP;
        vm_net_mock_role_sync_derived_vitals(role);
        return false;
    }
    acceptedExp = expCap - role->exp;
    if (addExp < acceptedExp)
        acceptedExp = addExp;
    role->exp += acceptedExp;
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
#define VM_NET_MOCK_MONSTER_DROP_MAX 8u

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
    u8 dropCount;
    vm_net_mock_monster_stats stats;
    vm_net_mock_monster_drop drops[VM_NET_MOCK_MONSTER_DROP_MAX];
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
    /* task.dsh type-2 kill target: 冥火麒麟【燎天】 (task 5004). */
    { 23, 40, VM_NET_MOCK_MONSTER_BOSS, 0, 0},
    { 25,  4, VM_NET_MOCK_MONSTER_UNDEAD, 43, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 28,  7, VM_NET_MOCK_MONSTER_FLYING, 45, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 29,  8, VM_NET_MOCK_MONSTER_FLYING, 0, 0},
    { 30,  8, VM_NET_MOCK_MONSTER_STONE, 47, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 31,  9, VM_NET_MOCK_MONSTER_HUMANOID, 0, 0},
    { 32, 10, VM_NET_MOCK_MONSTER_BEAST, 0, 0},
    { 34, 11, VM_NET_MOCK_MONSTER_UNDEAD, 51, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 36, 14, VM_NET_MOCK_MONSTER_STONE, 52, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    /* task.dsh type-2 kill target: 火凤凰 (task 112). */
    { 38, 30, VM_NET_MOCK_MONSTER_FLYING, 0, 0},
    { 40, 20, VM_NET_MOCK_MONSTER_SPIRIT, 0, 0},
    { 41, 20, VM_NET_MOCK_MONSTER_SLIME, 55, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    { 42, 22, VM_NET_MOCK_MONSTER_ELEMENTAL, 56, VM_NET_MOCK_TASK_MATERIAL_DROP_RATE},
    /* task.dsh type-2 kill target: 火蝮蛇 (task 407). */
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

/* Monster defaults are deliberately derived from the same no-equipment player
 * model that authoritative battle damage uses.  This is a balancing policy,
 * not a claim about an unrecovered original-server formula: all three jobs
 * are constructed at the requested level and the weakest survivability and
 * combat columns form the encounter floor. */
typedef struct
{
    u32 hp;
    u32 mp;
    u32 attack;
    u32 defense;
} vm_net_mock_monster_level_reference;

static u32 vm_net_mock_monster_scale_ceil(u32 value, u32 numerator,
                                          u32 denominator)
{
    uint64_t scaled;

    if (denominator == 0)
        return 0;
    scaled = (uint64_t)value * numerator;
    scaled = (scaled + denominator - 1u) / denominator;
    if (scaled > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX)
        return VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX;
    return (u32)scaled;
}

static void vm_net_mock_monster_level_reference_for_level(
    u32 requestedLevel, vm_net_mock_monster_level_reference *reference)
{
    vm_net_mock_role_state role;
    vm_net_mock_player_stats stats;
    u32 level = requestedLevel;

    if (reference == NULL)
        return;
    memset(reference, 0, sizeof(*reference));
    if (level == 0)
        level = 1;
    /* Role creation and level-up persistence cap real characters at 70.  A
     * malformed catalog level must not invent a stronger player baseline than
     * the server can actually create. */
    if (level > VM_NET_MOCK_ROLE_LEVEL_CAP)
        level = VM_NET_MOCK_ROLE_LEVEL_CAP;

    for (u32 job = 1; job <= 3; ++job)
    {
        memset(&role, 0, sizeof(role));
        role.level = level;
        role.job = job;
        vm_net_mock_role_build_base_player_stats(&role, &stats);
        if (job == 1 || stats.maxHp < reference->hp)
            reference->hp = stats.maxHp;
        if (job == 1 || stats.maxMp < reference->mp)
            reference->mp = stats.maxMp;
        if (job == 1 || stats.attack < reference->attack)
            reference->attack = stats.attack;
        if (job == 1 || stats.defense < reference->defense)
            reference->defense = stats.defense;
    }
}

static void vm_net_mock_monster_family_scale(
    vm_net_mock_monster_family family,
    u32 *hpScaleOut, u32 *mpScaleOut, u32 *attackScaleOut,
    u32 *defenseScaleOut)
{
    u32 hp = 1000;
    u32 mp = 1000;
    u32 attack = 1000;
    u32 defense = 1000;

    switch (family)
    {
    case VM_NET_MOCK_MONSTER_SLIME:
        hp = 850; mp = 1100; attack = 850; defense = 750;
        break;
    case VM_NET_MOCK_MONSTER_FLYING:
        hp = 850; mp = 1000; attack = 1100; defense = 750;
        break;
    case VM_NET_MOCK_MONSTER_INSECT:
        hp = 800; mp = 1000; attack = 1050; defense = 700;
        break;
    case VM_NET_MOCK_MONSTER_REPTILE:
        hp = 1000; mp = 1100; attack = 1050; defense = 1000;
        break;
    case VM_NET_MOCK_MONSTER_UNDEAD:
        hp = 1200; mp = 850; attack = 1000; defense = 1200;
        break;
    case VM_NET_MOCK_MONSTER_SPIRIT:
        hp = 900; mp = 1300; attack = 1150; defense = 900;
        break;
    case VM_NET_MOCK_MONSTER_ELEMENTAL:
        hp = 1100; mp = 1500; attack = 1200; defense = 1000;
        break;
    case VM_NET_MOCK_MONSTER_STONE:
        hp = 1400; mp = 1000; attack = 900; defense = 1450;
        break;
    case VM_NET_MOCK_MONSTER_HUMANOID:
        hp = 1000; mp = 1000; attack = 1100; defense = 1000;
        break;
    case VM_NET_MOCK_MONSTER_SOLDIER:
        hp = 1150; mp = 850; attack = 1150; defense = 1200;
        break;
    case VM_NET_MOCK_MONSTER_BEAST:
    default:
        break;
    }
    if (hpScaleOut)
        *hpScaleOut = hp;
    if (mpScaleOut)
        *mpScaleOut = mp;
    if (attackScaleOut)
        *attackScaleOut = attack;
    if (defenseScaleOut)
        *defenseScaleOut = defense;
}

static vm_net_mock_monster_stats vm_net_mock_monster_base_stats_for_enemy(u32 enemyId)
{
    vm_net_mock_monster_entry entry = vm_net_mock_monster_entry_for_enemy(enemyId);
    vm_net_mock_monster_stats stats;
    u32 level = entry.level ? entry.level : 1;
    vm_net_mock_monster_level_reference reference;
    u32 hpScale = 1000;
    u32 mpScale = 1000;
    u32 attackScale = 1000;
    u32 defenseScale = 1000;
    u32 normalHp = 0;
    u32 normalMp = 0;
    u32 normalAttack = 0;
    u32 normalDefense = 0;

    memset(&stats, 0, sizeof(stats));
    stats.enemyId = entry.enemyId;
    stats.level = level;
    vm_net_mock_monster_level_reference_for_level(level, &reference);
    normalHp = vm_net_mock_monster_scale_ceil(reference.attack, 5, 1);
    normalMp = 8 + level * 3;
    normalAttack = vm_net_mock_monster_scale_ceil(
        reference.hp, 100 + reference.defense, 1600);
    normalDefense = vm_net_mock_monster_scale_ceil(reference.attack, 30, 100);

    switch ((vm_net_mock_monster_family)entry.family)
    {
    case VM_NET_MOCK_MONSTER_SLIME:
        stats.exp = 3 + level * 2;
        stats.gold = 3 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_BEAST:
        stats.exp = 4 + level * 3;
        stats.gold = 3 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_FLYING:
        stats.exp = 4 + level * 3;
        stats.gold = 3 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_INSECT:
        stats.exp = 4 + level * 3;
        stats.gold = 3 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_REPTILE:
        stats.exp = 5 + level * 3;
        stats.gold = 4 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_UNDEAD:
        stats.exp = 6 + level * 3;
        stats.gold = 4 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_SPIRIT:
        stats.exp = 6 + level * 3;
        stats.gold = 5 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_ELEMENTAL:
        stats.exp = 7 + level * 3;
        stats.gold = 5 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_STONE:
        stats.exp = 8 + level * 3;
        stats.gold = 5 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_HUMANOID:
        stats.exp = 6 + level * 3;
        stats.gold = 6 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_SOLDIER:
        stats.exp = 7 + level * 3;
        stats.gold = 7 + level * 2;
        break;
    case VM_NET_MOCK_MONSTER_BOSS:
        /* Bosses must be defeated through party durability and combined damage,
         * not through an artificial admission check.  A level-matched naked
         * solo role falls before it can exhaust this pool, while a real party
         * with coordinated healing, equipment and combined actions can make
         * progress through the unchanged team-battle contract. */
        stats.hp = vm_net_mock_monster_scale_ceil(reference.attack, 28, 1);
        stats.mp = 24 + level * 6;
        stats.attack = vm_net_mock_monster_scale_ceil(
            reference.hp, 100 + reference.defense, 750);
        stats.defense = vm_net_mock_monster_scale_ceil(reference.attack, 60, 100);
        stats.exp = 20 + level * 5;
        stats.gold = 25 + level * 4;
        break;
    default:
        stats.exp = 4 + level * 3;
        stats.gold = 3 + level * 2;
        break;
    }

    if (entry.family != VM_NET_MOCK_MONSTER_BOSS)
    {
        vm_net_mock_monster_family_scale((vm_net_mock_monster_family)entry.family,
                                         &hpScale, &mpScale,
                                         &attackScale, &defenseScale);
        stats.hp = vm_net_mock_monster_scale_ceil(normalHp, hpScale, 1000);
        stats.mp = vm_net_mock_monster_scale_ceil(normalMp, mpScale, 1000);
        stats.attack = vm_net_mock_monster_scale_ceil(normalAttack,
                                                       attackScale, 1000);
        stats.defense = vm_net_mock_monster_scale_ceil(normalDefense,
                                                        defenseScale, 1000);
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
    u32 dropsLoaded;
    u32 dropsSkipped;
} vm_net_mock_monster_db_load_context;

static bool vm_net_mock_monster_db_row(void *contextValue,
                                       unsigned int columnCount,
                                       const char *const *values,
                                       const size_t *lengths)
{
    vm_net_mock_monster_db_load_context *context =
        (vm_net_mock_monster_db_load_context *)contextValue;
    u32 number[9];
    int index = -1;
    vm_net_mock_monster_override *override = NULL;

    memset(number, 0, sizeof(number));
    if (context == NULL || columnCount != 9)
        return false;
    for (u32 i = 0; i < 9; ++i)
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
        number[8] > VM_NET_MOCK_MONSTER_ADMIN_STAT_MAX)
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
        vm_net_mock_find_shop_catalog_item(number[2]) == NULL)
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
            "SELECT monster_id,level,family,hp,mp,attack_value,defense_value,"
            "reward_exp,reward_money "
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
    printf("[info][mock-admin] monster_db_load rows=%u skipped=%u drops=%u drops_skipped=%u\n",
           context.loaded, context.skipped, context.dropsLoaded,
           context.dropsSkipped);
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

/* A persisted administrator row chooses the effective monster type as well as
 * its explicit combat values.  Encounter cardinality must follow that type so
 * a monster changed to a boss cannot be randomly expanded into multiple boss
 * copies by the scene encounter roller. */
static vm_net_mock_monster_family vm_net_mock_monster_family_for_enemy(u32 enemyId)
{
    int index = vm_net_mock_monster_catalog_index(enemyId);

    (void)vm_net_mock_monster_db_load();
    if (index >= 0 && g_vm_net_mock_monster_overrides[index].used)
        return (vm_net_mock_monster_family)
            g_vm_net_mock_monster_overrides[index].family;
    return (vm_net_mock_monster_family)
        vm_net_mock_monster_entry_for_enemy(enemyId).family;
}

/* The source catalog predates backend editing and stores one legacy drop per
 * monster.  MySQL overrides deliberately own the complete list, including an
 * explicitly empty list, so an administrator can remove a built-in material
 * drop without it reappearing after reload. */
static u8 vm_net_mock_monster_drops_for_enemy(
    u32 enemyId, vm_net_mock_monster_drop *drops, u8 dropCap)
{
    int index = vm_net_mock_monster_catalog_index(enemyId);
    vm_net_mock_monster_entry entry = vm_net_mock_monster_entry_for_enemy(enemyId);
    u8 total = 0;

    (void)vm_net_mock_monster_db_load();
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
    char query[1024];
    char mysqlError[512];
    int index = -1;
    vm_net_mock_monster_override *override = NULL;
    bool transactionStarted = false;

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
        row->dropCount > VM_NET_MOCK_MONSTER_DROP_MAX)
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
    for (u8 i = 0; i < row->dropCount; ++i)
    {
        if (row->drops[i].itemId == 0 || row->drops[i].ratePercent == 0 ||
            row->drops[i].ratePercent > 100u ||
            vm_net_mock_find_shop_catalog_item(row->drops[i].itemId) == NULL)
        {
            if (errorOut)
                *errorOut = "掉落物品 ID 或概率无效";
            return false;
        }
        for (u8 previous = 0; previous < i; ++previous)
        {
            if (row->drops[previous].itemId == row->drops[i].itemId)
            {
                if (errorOut)
                    *errorOut = "同一怪物不能重复配置相同掉落物品";
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

    snprintf(
        query, sizeof(query),
        "INSERT INTO server_monsters(monster_id,level,family,hp,mp,attack_value,"
        "defense_value,reward_exp,reward_money,drop_item_id,drop_rate_percent) "
        "VALUES(%u,%u,%u,%u,%u,%u,%u,%u,%u,0,0) ON DUPLICATE KEY UPDATE "
        "level=VALUES(level),family=VALUES(family),hp=VALUES(hp),mp=VALUES(mp),"
        "attack_value=VALUES(attack_value),defense_value=VALUES(defense_value),"
        "reward_exp=VALUES(reward_exp),reward_money=VALUES(reward_money),"
        "drop_item_id=0,drop_rate_percent=0",
        row->enemyId, row->level, row->family, row->hp, row->mp,
        row->attack, row->defense, row->exp, row->gold);
    if (!vm_mysql_exec("START TRANSACTION"))
        goto mysql_failed;
    transactionStarted = true;
    if (!vm_mysql_exec(query))
        goto mysql_failed;
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
    override->stats.enemyId = row->enemyId;
    override->stats.level = row->level;
    override->stats.hp = row->hp;
    override->stats.mp = row->mp;
    override->stats.attack = row->attack;
    override->stats.defense = row->defense;
    override->stats.exp = row->exp;
    override->stats.gold = row->gold;
    override->dropCount = row->dropCount;
    if (row->dropCount != 0)
    {
        memcpy(override->drops, row->drops,
               sizeof(override->drops[0]) * row->dropCount);
    }
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] monster_save id=%u level=%u family=%u hp=%u mp=%u attack=%u defense=%u exp=%u money=%u drops=%u\n",
           row->enemyId, row->level, row->family, row->hp, row->mp,
           row->attack, row->defense, row->exp, row->gold,
           row->dropCount);
    return true;

mysql_failed:
    snprintf(mysqlError, sizeof(mysqlError), "%s", vm_mysql_last_error());
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    printf("[error][mock-admin] monster_save_failed id=%u drops=%u error=%s\n",
           row->enemyId, row->dropCount, mysqlError);
    if (errorOut)
        *errorOut = "怪物配置保存失败，请检查服务端 MySQL 日志";
    return false;
}

static bool vm_net_mock_monster_admin_reset(u32 enemyId,
                                            const char **errorOut)
{
    char query[256];
    char mysqlError[512];
    int index = vm_net_mock_monster_catalog_index(enemyId);
    bool transactionStarted = false;

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
        *errorOut = "恢复怪物默认失败，请检查服务端 MySQL 日志";
    return false;
}

static u32 vm_net_mock_battle_role_attack_default(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_player_stats stats;
    vm_net_mock_role_build_player_stats(role, &stats);
    vm_net_mock_battle_apply_active_stat_modifier(&stats);
    return stats.attack ? stats.attack : 1;
}

static u32 vm_net_mock_battle_role_defense_default(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    vm_net_mock_player_stats stats;
    vm_net_mock_role_build_player_stats(role, &stats);
    vm_net_mock_battle_apply_active_stat_modifier(&stats);
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
    vm_net_mock_battle_apply_active_stat_modifier(&playerStats);
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
    u32 itemId;

    if (role == NULL)
        return;
    memset(role->equippedItems, 0, sizeof(role->equippedItems));
    itemId = vm_net_mock_role_default_weapon_for_job(role->job);
    role->equippedItems[0].itemId = itemId;
    role->equippedItems[0].durabilityMax =
        vm_net_mock_equipment_durability_max_for_item(itemId);
    role->equippedItems[0].durability = role->equippedItems[0].durabilityMax;
}

static void vm_net_mock_role_init_default_backpack(vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    memset(role->backpackItems, 0, sizeof(role->backpackItems));
    role->backpackCapacity = VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY;
    role->backpackItemCount = 0;
    role->nextBackpackSeq = VM_NET_MOCK_BACKPACK_INSTANCE_SEQ_FIRST;
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
    for (u32 i = 0; i < VM_NET_MOCK_BACKPACK_LEGACY_MAX_ITEMS; ++i)
    {
        dst->backpackItems[i].itemId = src->backpackItems[i].itemId;
        dst->backpackItems[i].seq = src->backpackItems[i].seq;
        dst->backpackItems[i].enhanceLevel = src->backpackItems[i].enhanceLevel;
        dst->backpackItems[i].count = src->backpackItems[i].count;
    }
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
    for (u32 i = 0; i < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++i)
        dst->equippedItems[i].itemId = src->equippedItemIds[i];
    for (u32 i = 0; i < VM_NET_MOCK_BACKPACK_LEGACY_MAX_ITEMS; ++i)
    {
        dst->backpackItems[i].itemId = src->backpackItems[i].itemId;
        dst->backpackItems[i].seq = src->backpackItems[i].seq;
        dst->backpackItems[i].enhanceLevel = src->backpackItems[i].enhanceLevel;
        dst->backpackItems[i].count = src->backpackItems[i].count;
    }
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
    for (u32 i = 0; i < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++i)
        dst->equippedItems[i].itemId = src->equippedItemIds[i];
    for (u32 i = 0; i < VM_NET_MOCK_BACKPACK_MAX_ITEMS; ++i)
    {
        dst->backpackItems[i].itemId = src->backpackItems[i].itemId;
        dst->backpackItems[i].seq = src->backpackItems[i].seq;
        dst->backpackItems[i].enhanceLevel = src->backpackItems[i].enhanceLevel;
        dst->backpackItems[i].count = src->backpackItems[i].count;
    }
}

static void vm_net_mock_role_copy_from_v5(vm_net_mock_role_state *dst,
                                          const vm_net_mock_role_state_v5 *src)
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
    dst->wcoin = src->wcoin;
    memcpy(dst->scene, src->scene, sizeof(dst->scene));
    dst->x = src->x;
    dst->y = src->y;
    dst->backpackItemCount = src->backpackItemCount;
    dst->designationId = src->designationId;
    dst->nextBackpackSeq = src->nextBackpackSeq;
    for (u32 i = 0; i < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++i)
        dst->equippedItems[i].itemId = src->equippedItemIds[i];
    for (u32 i = 0; i < VM_NET_MOCK_BACKPACK_MAX_ITEMS; ++i)
    {
        dst->backpackItems[i].itemId = src->backpackItems[i].itemId;
        dst->backpackItems[i].seq = src->backpackItems[i].seq;
        dst->backpackItems[i].enhanceLevel = src->backpackItems[i].enhanceLevel;
        dst->backpackItems[i].count = src->backpackItems[i].count;
    }
}

/* `equip.dsh` owns the maximum durability for a known equipment id.  The
 * durable instance owns its current value.  During the one-time v5 migration
 * the latter did not exist, so only this narrow bootstrap sets it full; later
 * operations must move or mutate the instance rather than recreate it. */
static void vm_net_mock_role_normalize_equipment_instances(
    vm_net_mock_role_state *role)
{
    if (role == NULL)
        return;
    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        vm_net_mock_equipped_item_state *item = &role->equippedItems[slot];
        const vm_net_mock_equipment_catalog_item *catalog = NULL;
        u16 expectedMax = 0;

        if (item->itemId == 0)
        {
            memset(item, 0, sizeof(*item));
            continue;
        }
        catalog = vm_net_mock_find_equipment_catalog_item(item->itemId);
        expectedMax = vm_net_mock_equipment_durability_max_for_item(item->itemId);
        if (catalog == NULL || catalog->slot != slot || expectedMax == 0)
        {
            /* Retain unresolved persisted ids for investigation instead of
             * silently replacing the player's item with a starter item. */
            printf("[warn][mock-service] equipment_instance_unresolved role=%u slot=%u item=%u source=equip.dsh\n",
                   role->roleId, slot, item->itemId);
            continue;
        }
        if (item->enhanceLevel > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
            item->enhanceLevel = VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL;
        if (item->durabilityMax == 0)
        {
            item->durabilityMax = expectedMax;
            item->durability = expectedMax;
        }
        else
        {
            item->durabilityMax = expectedMax;
            if (item->durability > expectedMax)
                item->durability = expectedMax;
        }
    }
}

static bool vm_net_mock_backpack_sequence_present(
    const vm_net_mock_backpack_item_state *items,
    u32 itemCount,
    u16 sequence)
{
    if (items == NULL || sequence == 0)
        return false;
    if (itemCount > VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        itemCount = VM_NET_MOCK_BACKPACK_MAX_ITEMS;
    for (u32 i = 0; i < itemCount; ++i)
    {
        if (items[i].itemId != 0 && items[i].count != 0 &&
            items[i].seq == sequence)
        {
            return true;
        }
    }
    return false;
}

static u32 vm_net_mock_role_count_reserved_backpack_sequences(
    const vm_net_mock_role_state *role)
{
    u32 count = 0;
    u32 itemCount = 0;

    if (role == NULL)
        return 0;
    itemCount = role->backpackItemCount;
    if (itemCount > VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        itemCount = VM_NET_MOCK_BACKPACK_MAX_ITEMS;
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        if (item->itemId != 0 && item->count != 0 &&
            item->seq < VM_NET_MOCK_BACKPACK_INSTANCE_SEQ_FIRST)
        {
            ++count;
        }
    }
    return count;
}

/* The client allocates worn records with seq=slot+1 and its 29/3 enhancement
 * completion path updates the first globally matching seq.  Allocate every
 * backpack identity outside 1..8, including on legacy-data normalization. */
static bool vm_net_mock_role_allocate_backpack_sequence(
    const vm_net_mock_role_state *role,
    const vm_net_mock_backpack_item_state *pendingItems,
    u32 pendingCount,
    u32 preferredSequence,
    u16 *sequenceOut)
{
    u32 rawCount = 0;
    u32 candidate = preferredSequence;

    if (sequenceOut != NULL)
        *sequenceOut = 0;
    if (role == NULL || sequenceOut == NULL)
        return false;
    rawCount = role->backpackItemCount;
    if (rawCount > VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        rawCount = VM_NET_MOCK_BACKPACK_MAX_ITEMS;
    if (pendingCount > VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        pendingCount = VM_NET_MOCK_BACKPACK_MAX_ITEMS;
    if (candidate < VM_NET_MOCK_BACKPACK_INSTANCE_SEQ_FIRST || candidate > 0xffffu)
        candidate = VM_NET_MOCK_BACKPACK_INSTANCE_SEQ_FIRST;

    for (u32 tried = 0;
         tried <= 0xffffu - VM_NET_MOCK_BACKPACK_INSTANCE_SEQ_FIRST;
         ++tried)
    {
        u16 sequence = (u16)candidate;
        if (!vm_net_mock_backpack_sequence_present(role->backpackItems, rawCount,
                                                    sequence) &&
            !vm_net_mock_backpack_sequence_present(pendingItems, pendingCount,
                                                    sequence))
        {
            *sequenceOut = sequence;
            return true;
        }
        ++candidate;
        if (candidate > 0xffffu)
            candidate = VM_NET_MOCK_BACKPACK_INSTANCE_SEQ_FIRST;
    }
    return false;
}

/* Returns whether this call changed the durable backpack representation.  The
 * load path uses that result to persist legacy repairs transactionally; merely
 * making an over-limit stack look valid in RAM would reintroduce the same
 * invalid item row on every login. */
static bool vm_net_mock_role_normalize_backpack(vm_net_mock_role_state *role)
{
    vm_net_mock_backpack_item_state compact[VM_NET_MOCK_BACKPACK_MAX_ITEMS];
    vm_net_mock_role_state before;
    u32 compactCount = 0;
    u16 maxSeq = VM_NET_MOCK_EQUIP_SLOT_COUNT;

    if (role == NULL)
        return false;
    before = *role;
    memset(compact, 0, sizeof(compact));
    if (role->backpackCapacity == 0)
        role->backpackCapacity = VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY;
    else if (role->backpackCapacity > VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT)
        role->backpackCapacity = VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT;
    if (role->backpackItemCount > VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        role->backpackItemCount = VM_NET_MOCK_BACKPACK_MAX_ITEMS;
    if (role->backpackItemCount > role->backpackCapacity)
        role->backpackItemCount = role->backpackCapacity;

    /* First establish the high-water sequence across the persisted rows.  A
     * historical 921 row may have been incorrectly stacked; splitting it
     * below must never reuse a sequence held by a later ordinary item. */
    for (u32 i = 0; i < role->backpackItemCount; ++i)
    {
        if (role->backpackItems[i].seq > maxSeq)
            maxSeq = role->backpackItems[i].seq;
    }

    for (u32 i = 0; i < role->backpackItemCount; ++i)
    {
        vm_net_mock_backpack_item_state item = role->backpackItems[i];
        u32 instanceCount = 1;
        const vm_net_mock_equipment_catalog_item *equipment = NULL;
        u16 expectedMax = 0;
        u32 stackLimit = 0;
        if (item.itemId == 0 || item.count == 0)
            continue;
        if (item.enhanceLevel > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
            item.enhanceLevel = VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL;
        if (item.seq < VM_NET_MOCK_BACKPACK_INSTANCE_SEQ_FIRST ||
            vm_net_mock_backpack_sequence_present(compact, compactCount, item.seq))
        {
            if (!vm_net_mock_role_allocate_backpack_sequence(
                    role, compact, compactCount, (u32)maxSeq + 1, &item.seq))
            {
                printf("[error][mock-service] backpack_sequence_namespace_unresolved role=%u item=%u reason=no-free-sequence\n",
                       role->roleId, item.itemId);
                return memcmp(&before, role, sizeof(before)) != 0;
            }
            if (item.seq > maxSeq)
                maxSeq = item.seq;
        }
        equipment = vm_net_mock_find_equipment_catalog_item(item.itemId);
        expectedMax = vm_net_mock_equipment_durability_max_for_item(item.itemId);
        stackLimit = vm_net_mock_item_effect_stack_limit(item.itemId);
        if (equipment != NULL && expectedMax != 0)
        {
            /* Equipment rows are identities, never stacks.  Old snapshots
             * could stack them by id, so expand only when the actual backpack
             * capacity can retain every recovered instance. */
            instanceCount = item.count;
            if (item.durabilityMax == 0)
            {
                item.durabilityMax = expectedMax;
                item.durability = expectedMax;
            }
            else
            {
                item.durabilityMax = expectedMax;
                if (item.durability > expectedMax)
                    item.durability = expectedMax;
            }
            if (instanceCount > 1 &&
                (compactCount + instanceCount > role->backpackCapacity ||
                 compactCount + instanceCount > VM_NET_MOCK_BACKPACK_MAX_ITEMS))
            {
                if (compactCount < role->backpackCapacity &&
                    compactCount < VM_NET_MOCK_BACKPACK_MAX_ITEMS)
                {
                    compact[compactCount++] = item;
                }
                printf("[error][mock-service] equipment_instance_stack_unresolved role=%u item=%u seq=%u count=%u capacity=%u\n",
                       role->roleId, item.itemId, item.seq, item.count,
                       role->backpackCapacity);
                continue;
            }
        }
        if (item.itemId == 921)
        {
            instanceCount = item.count;
            if (instanceCount > 1 &&
                (compactCount + instanceCount > role->backpackCapacity ||
                 compactCount + instanceCount > VM_NET_MOCK_BACKPACK_MAX_ITEMS))
            {
                /* There is room for this persisted row but not for all its
                 * invalid historical instances. Keep the source row intact,
                 * report the violated storage contract, and defer migration
                 * until the player has enough slots. Never write beyond the
                 * compact buffer or silently drop a book. */
                if (compactCount < role->backpackCapacity &&
                    compactCount < VM_NET_MOCK_BACKPACK_MAX_ITEMS)
                {
                    compact[compactCount++] = item;
                }
                printf("[error][mock-service] training_book_stack_unresolved role=%u seq=%u count=%u capacity=%u reason=insufficient-slots\n",
                       role->roleId, item.seq, item.count, role->backpackCapacity);
                continue;
            }
        }
        if (equipment == NULL && item.itemId != 921 && stackLimit != 0)
        {
            /* Ordinary item rows must honor item.dsh 堆叠数.  Older role
             * snapshots could have been created by the former unbounded
             * merge path, so split them before they are exposed to the
             * client.  Do not truncate an impossible migration: preserve the
             * original row and make the violated storage contract explicit. */
            instanceCount = item.count / stackLimit;
            if (item.count % stackLimit != 0)
                ++instanceCount;
            if (instanceCount > 1 &&
                (compactCount + instanceCount > role->backpackCapacity ||
                 compactCount + instanceCount > VM_NET_MOCK_BACKPACK_MAX_ITEMS))
            {
                if (compactCount < role->backpackCapacity &&
                    compactCount < VM_NET_MOCK_BACKPACK_MAX_ITEMS)
                {
                    compact[compactCount++] = item;
                }
                printf("[error][mock-service] backpack_stack_limit_unresolved role=%u item=%u seq=%u count=%u stack_limit=%u capacity=%u reason=insufficient-slots\n",
                       role->roleId, item.itemId, item.seq, item.count,
                       stackLimit, role->backpackCapacity);
                continue;
            }
        }
        for (u32 instance = 0; instance < instanceCount; ++instance)
        {
            vm_net_mock_backpack_item_state instanceItem = item;
            if (instance != 0)
            {
                if (!vm_net_mock_role_allocate_backpack_sequence(
                        role, compact, compactCount, (u32)maxSeq + 1,
                        &instanceItem.seq))
                {
                    printf("[error][mock-service] backpack_sequence_namespace_unresolved role=%u item=%u reason=no-free-split-sequence\n",
                           role->roleId, item.itemId);
                    return memcmp(&before, role, sizeof(before)) != 0;
                }
                if (instanceItem.seq > maxSeq)
                    maxSeq = instanceItem.seq;
            }
            if (equipment != NULL && expectedMax != 0)
                instanceItem.count = 1;
            if (item.itemId == 921)
                instanceItem.count = 1;
            if (equipment == NULL && item.itemId != 921 && stackLimit != 0)
            {
                u32 consumed = instance * stackLimit;
                u32 remaining = item.count > consumed ? item.count - consumed : 0;
                instanceItem.count = remaining > stackLimit ? stackLimit : remaining;
            }
            if (compactCount >= role->backpackCapacity ||
                compactCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
            {
                /* This branch is only reachable for ordinary capacity
                 * saturation; 921 expansion is preflight-checked above. */
                printf("[error][mock-service] backpack_normalize_capacity role=%u item=%u seq=%u count=%u capacity=%u\n",
                       role->roleId, item.itemId, item.seq, item.count,
                       role->backpackCapacity);
                instance = instanceCount;
                break;
            }
            compact[compactCount++] = instanceItem;
        }
    }
    memset(role->backpackItems, 0, sizeof(role->backpackItems));
    if (compactCount > 0)
        memcpy(role->backpackItems, compact, sizeof(compact[0]) * compactCount);
    role->backpackItemCount = (u8)compactCount;
    if (!vm_net_mock_role_allocate_backpack_sequence(
            role, NULL, 0, (u32)maxSeq + 1, &role->nextBackpackSeq))
    {
        printf("[error][mock-service] backpack_sequence_namespace_unresolved role=%u reason=no-next-sequence\n",
               role->roleId);
        role->nextBackpackSeq = 0;
    }
    return memcmp(&before, role, sizeof(before)) != 0;
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
    if (role->exp > vm_net_mock_role_exp_cap())
        role->exp = vm_net_mock_role_exp_cap();
    role->level = vm_net_mock_role_level_from_exp(role->exp);
    if (!vm_net_mock_designation_is_unlocked(role, vm_net_mock_designation_by_id(role->designationId)))
        role->designationId = vm_net_mock_role_best_designation(role)->id;
    vm_net_mock_role_normalize_equipment_instances(role);
    vm_net_mock_role_sync_derived_vitals(role);
    role->scene[sizeof(role->scene) - 1] = 0;
    if (!vm_net_mock_scene_name_is_persistable(role->scene))
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

/* A battle session serial prevents duplicate status packets from granting a
 * second reward, but it is deliberately process-local.  The rate limit is a
 * different contract: it belongs to the persisted account/role identity and
 * must survive reconnects and a service restart. */
typedef struct
{
    uint64_t value;
    bool found;
    bool invalid;
} vm_mock_mysql_monster_reward_cooldown_context;

static bool g_vm_net_mock_monster_reward_cooldown_schema_prepared = false;

static bool vm_mock_mysql_monster_reward_cooldown_row(
    void *context_value, unsigned int column_count,
    const char *const *values, const size_t *lengths)
{
    vm_mock_mysql_monster_reward_cooldown_context *context =
        (vm_mock_mysql_monster_reward_cooldown_context *)context_value;

    if (context == NULL || context->found || column_count != 1 ||
        !vm_mock_mysql_parse_u64(values[0], lengths[0], &context->value))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_net_mock_role_prepare_monster_reward_cooldown_schema(void)
{
    if (g_vm_net_mock_monster_reward_cooldown_schema_prepared)
        return true;
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_role_monster_reward_cooldowns ("
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,"
            "last_reward_ms BIGINT UNSIGNED NOT NULL,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(account_id,role_id),"
            "CONSTRAINT fk_account_role_monster_reward_cooldowns_role "
            "FOREIGN KEY(account_id,role_id) REFERENCES account_roles(account_id,role_id) "
            "ON DELETE CASCADE"
            ") ENGINE=InnoDB"))
    {
        printf("[error][mock-service] monster_reward_cooldown_schema_prepare error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_monster_reward_cooldown_schema_prepared = true;
    return true;
}

/* Returns false for a storage/identity failure.  A successful call with
 * grantedOut=false means a previous monster victory for this exact role is
 * still inside the server-authoritative eight-second window. */
static bool vm_net_mock_role_try_claim_monster_reward_cooldown(
    const vm_net_mock_role_state *role, bool *grantedOut, u32 *remainingMsOut)
{
    char account_hex[129];
    char query[2048];
    char mysql_error[512];
    vm_mock_mysql_monster_reward_cooldown_context now_context;
    vm_mock_mysql_monster_reward_cooldown_context previous_context;
    bool transaction_started = false;
    uint64_t elapsed_ms = 0;
    uint64_t remaining_ms = 0;

    if (grantedOut)
        *grantedOut = false;
    if (remainingMsOut)
        *remainingMsOut = 0;
    if (role == NULL || role->roleId == 0 ||
        !vm_net_mock_mysql_account_hex(account_hex) ||
        !vm_net_mock_role_prepare_monster_reward_cooldown_schema())
    {
        return false;
    }

    memset(&now_context, 0, sizeof(now_context));
    memset(&previous_context, 0, sizeof(previous_context));
    if (!vm_mysql_exec("START TRANSACTION"))
        goto failed;
    transaction_started = true;
    if (!vm_mysql_query(
            "SELECT CAST(FLOOR(UNIX_TIMESTAMP(CURRENT_TIMESTAMP(3))*1000) AS UNSIGNED)",
            vm_mock_mysql_monster_reward_cooldown_row, &now_context) ||
        now_context.invalid || !now_context.found)
    {
        goto failed;
    }

    snprintf(query, sizeof(query),
             "SELECT last_reward_ms FROM account_role_monster_reward_cooldowns "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u FOR UPDATE",
             account_hex, role->roleId);
    if (!vm_mysql_query(query, vm_mock_mysql_monster_reward_cooldown_row,
                        &previous_context) ||
        previous_context.invalid)
    {
        goto failed;
    }

    if (previous_context.found)
    {
        if (now_context.value < previous_context.value)
            remaining_ms = VM_NET_MOCK_BATTLE_REWARD_COOLDOWN_MS;
        else
        {
            elapsed_ms = now_context.value - previous_context.value;
            if (elapsed_ms < VM_NET_MOCK_BATTLE_REWARD_COOLDOWN_MS)
                remaining_ms = VM_NET_MOCK_BATTLE_REWARD_COOLDOWN_MS - elapsed_ms;
        }
        if (remaining_ms != 0)
        {
            if (!vm_mysql_exec("COMMIT"))
                goto failed;
            transaction_started = false;
            if (remainingMsOut)
                *remainingMsOut = (u32)remaining_ms;
            return true;
        }
    }

    snprintf(query, sizeof(query),
             "INSERT INTO account_role_monster_reward_cooldowns "
             "(account_id,role_id,last_reward_ms) "
             "VALUES(CAST(X'%s' AS CHAR),%u,%llu) "
             "ON DUPLICATE KEY UPDATE last_reward_ms=VALUES(last_reward_ms)",
             account_hex, role->roleId, (unsigned long long)now_context.value);
    if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        goto failed;
    transaction_started = false;
    if (grantedOut)
        *grantedOut = true;
    return true;

failed:
    snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
    if (transaction_started)
        (void)vm_mysql_exec("ROLLBACK");
    printf("[error][mock-service] monster_reward_cooldown_claim_failed account=%s role=%u error=%s\n",
           g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
           role ? role->roleId : 0, mysql_error);
    return false;
}

/* This is deliberately a read-only companion to the transactional claim
 * above.  Automatic scene hangup needs to know whether it may dispatch a
 * killing action without creating an unrepresentable zero-reward automatic
 * terminal.  The database clock remains the authority; scheduler ticks only
 * delay a later poll and are never used to decide eligibility. */
static bool vm_net_mock_role_get_monster_reward_cooldown_remaining(
    const vm_net_mock_role_state *role, u32 *remainingMsOut)
{
    char account_hex[129];
    char query[2048];
    vm_mock_mysql_monster_reward_cooldown_context context;

    if (remainingMsOut != NULL)
        *remainingMsOut = 0;
    if (role == NULL || role->roleId == 0 ||
        !vm_net_mock_mysql_account_hex(account_hex) ||
        !vm_net_mock_role_prepare_monster_reward_cooldown_schema())
    {
        return false;
    }

    memset(&context, 0, sizeof(context));
    /* CURRENT_TIMESTAMP(3) is constant for one statement.  Keeping the clock
     * in the derived row avoids a host-time approximation and treats a server
     * clock rollback exactly like the claim path: a full cooldown remains. */
    snprintf(query, sizeof(query),
             "SELECT CAST(CASE "
             "WHEN reward_clock.now_ms < last_reward_ms THEN %u "
             "WHEN reward_clock.now_ms - last_reward_ms < %u THEN "
             "%u - (reward_clock.now_ms - last_reward_ms) "
             "ELSE 0 END AS UNSIGNED) "
             "FROM account_role_monster_reward_cooldowns "
             "CROSS JOIN (SELECT CAST(FLOOR(UNIX_TIMESTAMP(CURRENT_TIMESTAMP(3))*1000) "
             "AS UNSIGNED) AS now_ms) AS reward_clock "
             "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u",
             VM_NET_MOCK_BATTLE_REWARD_COOLDOWN_MS,
             VM_NET_MOCK_BATTLE_REWARD_COOLDOWN_MS,
             VM_NET_MOCK_BATTLE_REWARD_COOLDOWN_MS,
             account_hex, role->roleId);
    if (!vm_mysql_query(query, vm_mock_mysql_monster_reward_cooldown_row,
                        &context) || context.invalid ||
        (context.found && context.value > VM_NET_MOCK_BATTLE_REWARD_COOLDOWN_MS))
    {
        printf("[error][mock-service] monster_reward_cooldown_read_failed "
               "account=%s role=%u error=%s\n",
               g_vm_mock_service_active_account_id ?
                   g_vm_mock_service_active_account_id : "-",
               role->roleId, vm_mysql_last_error());
        return false;
    }
    if (remainingMsOut != NULL && context.found)
        *remainingMsOut = (u32)context.value;
    return true;
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
    bool found;
    bool invalid;
} vm_mock_mysql_role_item_effect_context;

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
               (effect->itemId == 811 && effect->multiplier == 10);
    }
    if (effect->kind == VM_NET_MOCK_ROLE_ITEM_EFFECT_COMBAT_PILL)
    {
        /* item.dsh proves the duration but contains no numeric stat modifier. */
        return (effect->itemId == 829 || effect->itemId == 830) &&
               effect->multiplier == 0;
    }
    if (effect->kind == VM_NET_MOCK_ROLE_ITEM_EFFECT_BATTLE_INSIGHT)
    {
        return effect->itemId == 828 && effect->multiplier == 20;
    }
    return false;
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

    if (context == NULL || context->found || column_count != 3 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &item_id) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &multiplier) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &expires_unix) ||
        multiplier > 255)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->effect.itemId = item_id;
    context->effect.multiplier = multiplier;
    context->effect.expiresUnix = expires_unix;
    context->found = true;
    return true;
}

/* Returns false only for a storage or contract error.  A zero expiresUnix in
 * effectOut means that no currently active record exists. */
static bool vm_net_mock_role_get_active_timed_item_effect(
    const vm_net_mock_role_state *role, u8 effect_kind,
    vm_net_mock_role_item_effect *effectOut)
{
    char account_hex[129];
    char query[768];
    vm_mock_mysql_role_item_effect_context context;
    u32 now = (u32)time(NULL);

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
             "SELECT item_id,multiplier,expires_unix FROM account_role_item_effects "
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
    if (context.effect.expiresUnix <= now)
    {
        snprintf(query, sizeof(query),
                 "DELETE FROM account_role_item_effects WHERE "
                 "account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND effect_kind=%u "
                 "AND expires_unix<=%u",
                 account_hex, role->roleId, effect_kind, now);
        if (!vm_mysql_exec(query))
            return false;
        return true;
    }
    if (!vm_net_mock_role_item_effect_is_valid(&context.effect))
    {
        printf("[error][mock-service] item_effect_invalid account=%s role=%u kind=%u item=%u multiplier=%u expires=%u\n",
               g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
               role->roleId, effect_kind, context.effect.itemId,
               context.effect.multiplier, context.effect.expiresUnix);
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
    bool equipmentInstanceBackfillNeeded;
    bool expCurveMigrationNeeded;
    u8 seenRoleMask;
    u8 roleRows;
} vm_mock_mysql_role_load_context;

typedef struct
{
    bool found;
    bool invalid;
} vm_mock_mysql_schema_column_context;

static bool g_vm_net_mock_equipment_instance_schema_prepared = false;

static bool vm_mock_mysql_schema_column_row(void *context_value,
                                            unsigned int column_count,
                                            const char *const *values,
                                            const size_t *lengths)
{
    vm_mock_mysql_schema_column_context *context =
        (vm_mock_mysql_schema_column_context *)context_value;
    if (context == NULL || context->found || column_count != 1 ||
        values[0] == NULL || lengths[0] == 0)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_net_mock_role_ensure_equipment_instance_column(
    const char *table_name, const char *column_name, const char *alter_sql)
{
    char query[512];
    vm_mock_mysql_schema_column_context context;

    if (table_name == NULL || column_name == NULL || alter_sql == NULL)
        return false;
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT COLUMN_NAME FROM information_schema.COLUMNS "
             "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='%s' "
             "AND COLUMN_NAME='%s'",
             table_name, column_name);
    if (!vm_mysql_query(query, vm_mock_mysql_schema_column_row, &context) ||
        context.invalid)
    {
        return false;
    }
    return context.found || vm_mysql_exec(alter_sql);
}

/* Relational saves become the sole live authority for an equipment instance.
 * The ALTERs are deliberately column-by-column so existing MySQL 5.x servers
 * are supported; the equivalent deployment script lives beside schema.sql. */
static bool vm_net_mock_role_prepare_equipment_instance_schema(void)
{
    if (g_vm_net_mock_equipment_instance_schema_prepared)
        return true;
    /* Legacy deployments created this table lazily when durability was first
     * used.  Keep it available only as a migration source; live reads and
     * writes use the equipment-instance columns below. */
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_role_equipment_durability ("
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,slot_index TINYINT UNSIGNED NOT NULL,"
            "item_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "durability SMALLINT UNSIGNED NOT NULL,durability_max SMALLINT UNSIGNED NOT NULL,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(account_id,role_id,slot_index)) ENGINE=InnoDB") ||
        !vm_net_mock_role_ensure_equipment_instance_column(
            "account_role_equipment", "enhance_level",
            "ALTER TABLE account_role_equipment ADD COLUMN enhance_level "
            "SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER item_id") ||
        !vm_net_mock_role_ensure_equipment_instance_column(
            "account_role_equipment", "durability",
            "ALTER TABLE account_role_equipment ADD COLUMN durability "
            "SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER enhance_level") ||
        !vm_net_mock_role_ensure_equipment_instance_column(
            "account_role_equipment", "durability_max",
            "ALTER TABLE account_role_equipment ADD COLUMN durability_max "
            "SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER durability") ||
        !vm_net_mock_role_ensure_equipment_instance_column(
            "account_role_backpack", "durability",
            "ALTER TABLE account_role_backpack ADD COLUMN durability "
            "SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER enhance_level") ||
        !vm_net_mock_role_ensure_equipment_instance_column(
            "account_role_backpack", "durability_max",
            "ALTER TABLE account_role_backpack ADD COLUMN durability_max "
            "SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER durability"))
    {
        printf("[error][mock-service] equipment_instance_schema_prepare error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_equipment_instance_schema_prepared = true;
    printf("[info][mock-service] equipment_instance_schema_prepare storage=relational\n");
    return true;
}

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
        (format_version != 5 && format_version != 6 &&
         format_version != VM_NET_MOCK_ROLE_DB_VERSION) ||
        role_count > VM_NET_MOCK_ROLE_DB_MAX_ROLES)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    memcpy(context->database->magic, "JHR1", 4);
    context->database->version = VM_NET_MOCK_ROLE_DB_VERSION;
    context->database->activeRoleId = active_role_id;
    context->database->roleCount = role_count;
    context->equipmentInstanceBackfillNeeded = format_version == 5;
    context->expCurveMigrationNeeded = format_version < VM_NET_MOCK_ROLE_DB_VERSION;
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
    u32 enhance_level = 0;
    u32 durability = 0;
    u32 durability_max = 0;
    if (context == NULL || context->database == NULL || column_count != 6 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &role_id) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &slot_index) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &item_id) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &enhance_level) ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &durability) ||
        !vm_mock_mysql_parse_u32(values[5], lengths[5], &durability_max) ||
        enhance_level > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL ||
        durability > 0xffffu || durability_max > 0xffffu ||
        (durability_max != 0 && durability > durability_max) ||
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
    role->equippedItems[slot_index].itemId = item_id;
    role->equippedItems[slot_index].enhanceLevel = (u16)enhance_level;
    role->equippedItems[slot_index].durability = (u16)durability;
    role->equippedItems[slot_index].durabilityMax = (u16)durability_max;
    if (item_id != 0 && durability_max == 0)
        context->equipmentInstanceBackfillNeeded = true;
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
    u32 durability = 0;
    u32 durability_max = 0;
    if (context == NULL || context->database == NULL || column_count != 8 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &role_id) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &slot_index) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &item_id) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &item_seq) || item_seq > 65535 ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &item_count) ||
        !vm_mock_mysql_parse_u32(values[5], lengths[5], &enhance_level) ||
        !vm_mock_mysql_parse_u32(values[6], lengths[6], &durability) ||
        !vm_mock_mysql_parse_u32(values[7], lengths[7], &durability_max) ||
        enhance_level > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL ||
        durability > 0xffffu || durability_max > 0xffffu ||
        (durability_max != 0 && durability > durability_max) ||
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
    role->backpackItems[slot_index].durability = (u16)durability;
    role->backpackItems[slot_index].durabilityMax = (u16)durability_max;
    role->backpackItems[slot_index].count = item_count;
    if (item_id >= 1000 && durability_max == 0)
        context->equipmentInstanceBackfillNeeded = true;
    return true;
}

typedef struct
{
    u32 balance;
    bool found;
    bool invalid;
} vm_mock_account_wallet_balance_context;

typedef struct
{
    uint64_t value;
    bool found;
    bool invalid;
} vm_mock_account_wallet_u64_context;

typedef struct
{
    bool found;
    bool invalid;
} vm_mock_account_wallet_marker_context;

static bool vm_mock_account_wallet_balance_row(void *context_value,
                                               unsigned int column_count,
                                               const char *const *values,
                                               const size_t *lengths)
{
    vm_mock_account_wallet_balance_context *context =
        (vm_mock_account_wallet_balance_context *)context_value;

    if (context == NULL || context->found || column_count != 1 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &context->balance))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_mock_account_wallet_u64_row(void *context_value,
                                           unsigned int column_count,
                                           const char *const *values,
                                           const size_t *lengths)
{
    vm_mock_account_wallet_u64_context *context =
        (vm_mock_account_wallet_u64_context *)context_value;

    if (context == NULL || context->found || column_count != 1 ||
        !vm_mock_mysql_parse_u64(values[0], lengths[0], &context->value))
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_mock_account_wallet_marker_row(void *context_value,
                                              unsigned int column_count,
                                              const char *const *values,
                                              const size_t *lengths)
{
    vm_mock_account_wallet_marker_context *context =
        (vm_mock_account_wallet_marker_context *)context_value;
    const size_t marker_len = strlen(VM_MOCK_ACCOUNT_WCOIN_WALLET_MIGRATION);

    if (context == NULL || context->found || column_count != 1 || values[0] == NULL ||
        lengths[0] != marker_len ||
        memcmp(values[0], VM_MOCK_ACCOUNT_WCOIN_WALLET_MIGRATION, marker_len) != 0)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

static bool vm_mock_service_account_wallet_read(const char *account_id,
                                                bool for_update,
                                                u32 *balance_out)
{
    char account_hex[129];
    char query[512];
    vm_mock_account_wallet_balance_context context;

    if (balance_out)
        *balance_out = 0;
    if (account_id == NULL || account_id[0] == 0 ||
        vm_mysql_hex_encode(account_id, strlen(account_id), account_hex,
                            sizeof(account_hex)) == 0)
    {
        return false;
    }
    memset(&context, 0, sizeof(context));
    snprintf(query, sizeof(query),
             "SELECT wcoin FROM account_wallets WHERE account_id=CAST(X'%s' AS CHAR)%s",
             account_hex, for_update ? " FOR UPDATE" : "");
    if (!vm_mysql_query(query, vm_mock_account_wallet_balance_row, &context) ||
        context.invalid || !context.found)
    {
        return false;
    }
    if (balance_out)
        *balance_out = context.balance;
    return true;
}

/* This migration is deliberately separate from the historical payload and
 * relation migrations.  It runs only after all legacy role snapshots have
 * become account_roles rows, then moves every old role balance exactly once
 * inside the same InnoDB transaction that clears the old source column and
 * records the marker. */
static bool vm_mock_service_account_wallet_prepare_and_migrate(void)
{
    vm_mock_account_wallet_marker_context marker;
    vm_mock_account_wallet_u64_context maximum;
    char query[1024];
    bool transaction = false;

    if (!vm_mock_service_mysql_authority_prepare() ||
        !vm_mysql_exec("START TRANSACTION"))
    {
        return false;
    }
    transaction = true;
    memset(&marker, 0, sizeof(marker));
    snprintf(query, sizeof(query),
             "SELECT migration_name FROM server_data_migrations WHERE migration_name='%s' FOR UPDATE",
             VM_MOCK_ACCOUNT_WCOIN_WALLET_MIGRATION);
    if (!vm_mysql_query(query, vm_mock_account_wallet_marker_row, &marker) || marker.invalid)
        goto failed;
    if (marker.found)
    {
        if (!vm_mysql_exec("COMMIT"))
            goto failed;
        g_vm_mock_account_wcoin_wallet_migrated = true;
        return true;
    }

    /* Accounts without a role still need a stable zero wallet so later role
     * creation and payment-order history share the same parent account. */
    if (!vm_mysql_exec(
            "INSERT IGNORE INTO account_wallets(account_id,wcoin) "
            "SELECT account_id,0 FROM accounts"))
    {
        goto failed;
    }
    memset(&maximum, 0, sizeof(maximum));
    if (!vm_mysql_query(
            "SELECT COALESCE(MAX(wallet_total),0) FROM "
            "(SELECT SUM(wcoin) AS wallet_total FROM account_roles GROUP BY account_id) "
            "AS account_wallet_totals",
            vm_mock_account_wallet_u64_row, &maximum) ||
        maximum.invalid || !maximum.found || maximum.value > UINT32_MAX)
    {
        if (maximum.found && maximum.value > UINT32_MAX)
            printf("[error][mock-service] account_wcoin_wallet_migration overflow max=%llu limit=%u\n",
                   (unsigned long long)maximum.value, UINT32_MAX);
        goto failed;
    }
    if (!vm_mysql_exec(
            "UPDATE account_wallets AS wallet "
            "JOIN (SELECT account_id,SUM(wcoin) AS wallet_total "
            "FROM account_roles GROUP BY account_id) AS legacy "
            "ON legacy.account_id=wallet.account_id "
            "SET wallet.wcoin=legacy.wallet_total") ||
        !vm_mysql_exec("UPDATE account_roles SET wcoin=0 WHERE wcoin<>0") ||
        !vm_mysql_exec(
            "INSERT INTO server_data_migrations(migration_name) VALUES('"
            VM_MOCK_ACCOUNT_WCOIN_WALLET_MIGRATION "')") ||
        !vm_mysql_exec("COMMIT"))
    {
        goto failed;
    }
    transaction = false;
    g_vm_mock_account_wcoin_wallet_migrated = true;
    printf("[info][mock-service] account_wcoin_wallet_migration marker=%s source=account_roles destination=account_wallets action=committed\n",
           VM_MOCK_ACCOUNT_WCOIN_WALLET_MIGRATION);
    return true;

failed:
    if (transaction)
        (void)vm_mysql_exec("ROLLBACK");
    printf("[error][mock-service] account_wcoin_wallet_migration_failed marker=%s error=%s\n",
           VM_MOCK_ACCOUNT_WCOIN_WALLET_MIGRATION, vm_mysql_last_error());
    return false;
}

static bool vm_net_mock_role_db_load_mysql_relational(bool *found_out,
                                                       bool *backfill_needed_out,
                                                       bool *exp_curve_migration_out)
{
    char account_hex[129];
    char query[2048];
    vm_mock_mysql_role_load_context context;
    if (found_out)
        *found_out = false;
    if (backfill_needed_out)
        *backfill_needed_out = false;
    if (exp_curve_migration_out)
        *exp_curve_migration_out = false;
    if (!vm_net_mock_mysql_account_hex(account_hex))
        return false;
    if (!vm_net_mock_role_prepare_equipment_instance_schema())
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
             "SELECT e.role_id,e.slot_index,e.item_id,e.enhance_level,"
             "CASE WHEN e.durability_max=0 THEN COALESCE(d.durability,0) ELSE e.durability END,"
             "CASE WHEN e.durability_max=0 THEN COALESCE(d.durability_max,0) ELSE e.durability_max END "
             "FROM account_role_equipment e "
             "LEFT JOIN account_role_equipment_durability d "
             "ON d.account_id=e.account_id AND d.role_id=e.role_id "
             "AND d.slot_index=e.slot_index AND d.item_id=e.item_id "
             "WHERE e.account_id=CAST(X'%s' AS CHAR) ORDER BY e.role_id,e.slot_index",
             account_hex);
    if (!vm_mysql_query(query, vm_mock_mysql_role_equipment_row, &context) || context.invalid)
        return false;
    snprintf(query, sizeof(query),
             "SELECT role_id,slot_index,item_id,item_seq,item_count,enhance_level,durability,durability_max "
             "FROM account_role_backpack WHERE account_id=CAST(X'%s' AS CHAR) ORDER BY role_id,slot_index",
             account_hex);
    if (!vm_mysql_query(query, vm_mock_mysql_role_backpack_row, &context) || context.invalid)
        return false;
    if (g_vm_mock_account_wcoin_wallet_migrated)
    {
        u32 account_wcoin = 0;

        if (!vm_mock_service_account_wallet_read(g_vm_mock_service_active_account_id,
                                                 false, &account_wcoin))
        {
            return false;
        }
        /* The client has only role-shaped W-coin fields.  Hydrate every
         * loaded role with the one account balance so role selection cannot
         * split what is now an account-level currency. */
        for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
            g_vm_net_mock_role_db.roles[i].wcoin = account_wcoin;
    }
    if (found_out)
        *found_out = true;
    if (backfill_needed_out)
        *backfill_needed_out = context.equipmentInstanceBackfillNeeded;
    if (exp_curve_migration_out)
        *exp_curve_migration_out = context.expCurveMigrationNeeded;
    return true;
}

typedef struct
{
    vm_net_mock_role_db_file *database;
    bool found;
    bool invalid;
    bool expCurveMigrationNeeded;
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
        (format_version != 5 && format_version != 6 &&
         format_version != VM_NET_MOCK_ROLE_DB_VERSION) ||
        role_count > VM_NET_MOCK_ROLE_DB_MAX_ROLES)
    {
        if (context)
            context->invalid = true;
        return true;
    }
    context->expCurveMigrationNeeded = format_version < VM_NET_MOCK_ROLE_DB_VERSION;
    if (format_version == VM_NET_MOCK_ROLE_DB_VERSION)
    {
        if (!vm_mysql_hex_decode(values[3], lengths[3], context->database,
                                 sizeof(*context->database), &payload_len) ||
            payload_len != sizeof(*context->database) ||
            memcmp(context->database->magic, "JHR1", 4) != 0 ||
            context->database->version != format_version ||
            context->database->activeRoleId != active_role_id ||
            context->database->roleCount != role_count)
        {
            context->invalid = true;
            return true;
        }
    }
    else if (format_version == 6)
    {
        vm_net_mock_role_db_file legacy;
        memset(&legacy, 0, sizeof(legacy));
        if (!vm_mysql_hex_decode(values[3], lengths[3], &legacy, sizeof(legacy),
                                 &payload_len) ||
            payload_len != sizeof(legacy) || memcmp(legacy.magic, "JHR1", 4) != 0 ||
            legacy.version != format_version || legacy.activeRoleId != active_role_id ||
            legacy.roleCount != role_count)
        {
            context->invalid = true;
            return true;
        }
        *context->database = legacy;
        context->database->version = VM_NET_MOCK_ROLE_DB_VERSION;
    }
    else
    {
        vm_net_mock_role_db_file_v5 legacy;
        memset(&legacy, 0, sizeof(legacy));
        if (!vm_mysql_hex_decode(values[3], lengths[3], &legacy, sizeof(legacy),
                                 &payload_len) ||
            payload_len != sizeof(legacy) || memcmp(legacy.magic, "JHR1", 4) != 0 ||
            legacy.version != format_version || legacy.activeRoleId != active_role_id ||
            legacy.roleCount != role_count)
        {
            context->invalid = true;
            return true;
        }
        memset(context->database, 0, sizeof(*context->database));
        memcpy(context->database->magic, "JHR1", 4);
        context->database->version = VM_NET_MOCK_ROLE_DB_VERSION;
        context->database->activeRoleId = legacy.activeRoleId;
        context->database->roleCount = legacy.roleCount;
        for (u32 i = 0; i < legacy.roleCount; ++i)
            vm_net_mock_role_copy_from_v5(&context->database->roles[i],
                                          &legacy.roles[i]);
        vm_autotest_note("mock_role_db_migrate version=5->7 source=payload-backup roles=%u active=%u\n",
                         legacy.roleCount, legacy.activeRoleId);
    }
    context->found = true;
    return true;
}

static bool vm_net_mock_role_db_load_mysql_payload_backup(bool *found_out,
                                                           bool *exp_curve_migration_out)
{
    char account_hex[129];
    char query[512];
    vm_mock_mysql_payload_load_context context;
    if (found_out)
        *found_out = false;
    if (exp_curve_migration_out)
        *exp_curve_migration_out = false;
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
    if (exp_curve_migration_out)
        *exp_curve_migration_out = context.expCurveMigrationNeeded;
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

typedef struct
{
    u32 expectedBalance;
    u32 debit;
    u32 committedBalance;
} vm_net_mock_account_wallet_debit;

static void vm_mock_service_account_wallet_sync_cached_balance(
    const char *account_id, u32 balance);

static bool vm_net_mock_role_db_save_relational(const char *reason,
                                                 const u32 *old_ids,
                                                 const u32 *new_ids,
                                                 u32 mapping_count,
                                                 bool full_snapshot,
                                                 const vm_net_mock_role_item_effect *timed_effect,
                                                 vm_net_mock_account_wallet_debit *wallet_debit)
{
    const char *account_id = g_vm_mock_service_active_account_id;
    char account_hex[129];
    char query[3072];
    char mysql_error[512];
    char *bulk_query = NULL;
    size_t bulk_capacity = 131072;
    bool transaction_started = false;
    u32 scoped_role_id = 0;
    u32 wallet_balance = 0;
    mysql_error[0] = 0;

    if (!g_vm_net_mock_role_db_valid || !vm_net_mock_mysql_account_hex(account_hex))
        return false;
    if (timed_effect != NULL &&
        (!vm_net_mock_role_item_effect_is_valid(timed_effect) ||
         !vm_net_mock_role_prepare_item_effect_schema()))
    {
        return false;
    }
    memcpy(g_vm_net_mock_role_db.magic, "JHR1", 4);
    g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
    if (g_vm_net_mock_role_db.roleCount > VM_NET_MOCK_ROLE_DB_MAX_ROLES)
        g_vm_net_mock_role_db.roleCount = VM_NET_MOCK_ROLE_DB_MAX_ROLES;
    if (g_vm_net_mock_role_db.roleCount == 0)
        g_vm_net_mock_role_db.activeRoleId = 0;
    if (!full_snapshot)
    {
        if (g_vm_net_mock_role_db.roleCount == 0 ||
            g_vm_net_mock_role_db.activeRoleId == 0)
        {
            return false;
        }
        scoped_role_id = g_vm_net_mock_role_db.activeRoleId;
        bool found = false;
        for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        {
            if (g_vm_net_mock_role_db.roles[i].roleId == scoped_role_id)
            {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }

    /* account_role_training_books is companion state keyed by a backpack
     * instance.  Reconcile it for every role/backpack write, including a
     * write that removed the final 921 instance from the scope. */
    if (!vm_net_mock_training_book_schema_prepare())
        return false;

    bulk_query = (char *)malloc(bulk_capacity);
    if (bulk_query == NULL)
        return false;
    if (!vm_mysql_exec("START TRANSACTION"))
    {
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        goto failed;
    }
    transaction_started = true;
    if (wallet_debit != NULL)
    {
        wallet_debit->committedBalance = 0;
        if (wallet_debit->debit == 0 ||
            !vm_mock_service_account_wallet_read(account_id, true, &wallet_balance) ||
            wallet_balance != wallet_debit->expectedBalance ||
            wallet_balance < wallet_debit->debit)
        {
            snprintf(mysql_error, sizeof(mysql_error),
                     "account wallet debit rejected expected=%u actual=%u debit=%u",
                     wallet_debit->expectedBalance, wallet_balance,
                     wallet_debit->debit);
            goto failed;
        }
        wallet_debit->committedBalance = wallet_balance - wallet_debit->debit;
    }
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
    /* Keep existing role rows alive while reconciling a full role snapshot. Guilds,
     * guild_members and guild_applications intentionally reference these rows
     * with ON DELETE CASCADE, so the former delete-and-reinsert save erased a
     * role's guild every time role-select/position/battle state was persisted.
     * Delete only roles that really disappeared from the account, move the
     * surviving role_index values out of the live range to avoid unique-index
     * collisions, then upsert them in their current order. */
    if (full_snapshot && g_vm_net_mock_role_db.roleCount == 0)
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
    if (full_snapshot && !vm_mysql_exec(query))
    {
        snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
        goto failed;
    }
    if (full_snapshot && g_vm_net_mock_role_db.roleCount != 0)
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

    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        const vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
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
                 /* account_roles.wcoin is retired source data after the
                  * wallet marker commits.  The pre-marker exception preserves
                  * a legacy payload just long enough for that migration. */
                 role->mp, role->mpMax, role->money,
                 g_vm_mock_account_wcoin_wallet_migrated ? 0u : role->wcoin,
                 scene_hex,
                 role->x, role->y, role->backpackItemCount, role->designationId,
                 role->nextBackpackSeq);
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
                 "INSERT INTO account_role_item_effects(account_id,role_id,effect_kind,item_id,multiplier,expires_unix) "
                 "VALUES(CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u) "
                 "ON DUPLICATE KEY UPDATE item_id=VALUES(item_id),multiplier=VALUES(multiplier),"
                 "expires_unix=VALUES(expires_unix)",
                 account_hex, scoped_role_id, timed_effect->kind,
                 timed_effect->itemId, timed_effect->multiplier,
                 timed_effect->expiresUnix);
        if (!vm_mysql_exec(query))
        {
            snprintf(mysql_error, sizeof(mysql_error), "%s", vm_mysql_last_error());
            goto failed;
        }
    }

    size_t bulk_len = (size_t)snprintf(
        bulk_query, bulk_capacity,
        "INSERT INTO account_role_equipment(account_id,role_id,slot_index,item_id,enhance_level,durability,durability_max) VALUES");
    u32 bulk_rows = 0;
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        const vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
        if (!full_snapshot && role->roleId != scoped_role_id)
            continue;
        for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
        {
            const vm_net_mock_equipped_item_state *item = &role->equippedItems[slot];
            if (item->itemId == 0)
                continue;
            int written = snprintf(bulk_query + bulk_len, bulk_capacity - bulk_len,
                                   "%s(CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u,%u)",
                                   bulk_rows ? "," : "", account_hex, role->roleId,
                                   slot, item->itemId, item->enhanceLevel,
                                   item->durability, item->durabilityMax);
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
        "INSERT INTO account_role_backpack(account_id,role_id,slot_index,item_id,item_seq,item_count,enhance_level,durability,durability_max) VALUES");
    bulk_rows = 0;
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        const vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
        if (!full_snapshot && role->roleId != scoped_role_id)
            continue;
        for (u32 slot = 0; slot < VM_NET_MOCK_BACKPACK_MAX_ITEMS; ++slot)
        {
            const vm_net_mock_backpack_item_state *item = &role->backpackItems[slot];
            if (item->itemId == 0 && item->seq == 0 && item->count == 0)
                continue;
            int written = snprintf(bulk_query + bulk_len, bulk_capacity - bulk_len,
                                   "%s(CAST(X'%s' AS CHAR),%u,%u,%u,%u,%u,%u,%u,%u)",
                                   bulk_rows ? "," : "", account_hex, role->roleId,
                                   slot, item->itemId, item->seq, item->count,
                                   item->enhanceLevel, item->durability,
                                   item->durabilityMax);
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

    /* The book table references the just-written backpack identity. This must
     * run after the replacement of account_role_backpack, not after its
     * delete: otherwise orphan cleanup would erase every live instance before
     * it can be matched to its 921 row. */
    if (!vm_net_mock_training_book_sync_role_records(&g_vm_net_mock_role_db,
                                                      account_hex, full_snapshot,
                                                      scoped_role_id))
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
    if (wallet_debit != NULL)
    {
        snprintf(query, sizeof(query),
                 "UPDATE account_wallets SET wcoin=%u "
                 "WHERE account_id=CAST(X'%s' AS CHAR)",
                 wallet_debit->committedBalance, account_hex);
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
    if (wallet_debit != NULL)
        vm_mock_service_account_wallet_sync_cached_balance(
            account_id, wallet_debit->committedBalance);
    g_vm_net_mock_role_position_dirty = false;
    vm_autotest_note("mock_role_db_mysql_save account=%s reason=%s roles=%u active=%u scope=%s storage=relational\n",
                     account_id ? account_id : "-", reason ? reason : "state",
                     g_vm_net_mock_role_db.roleCount, g_vm_net_mock_role_db.activeRoleId,
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

static bool vm_net_mock_role_db_save(const char *reason)
{
    return vm_net_mock_role_db_save_relational(reason, NULL, NULL, 0, false, NULL, NULL);
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

static u8 vm_net_mock_role_active_exp_card_flag(void)
{
    return vm_net_mock_role_active_exp_card_multiplier(vm_net_mock_active_role()) > 1 ? 1 : 0;
}

/* item.dsh describes 战斗心得 as a one-hour, +20% experience status. Its
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

/* The backpack decrement and the timed effect belong to one durable action.
 * The row is only changed in memory before the relational transaction has
 * committed; on any failure restore the exact previous role state so a retry
 * cannot lose an item or create an unbacked effect. */
static bool vm_net_mock_role_consume_backpack_item_with_timed_effect(
    vm_net_mock_role_state *role, u32 itemId, u16 seq,
    const vm_net_mock_role_item_effect *effect, u32 *remainingOut,
    const char *reason)
{
    vm_net_mock_role_item_effect active;
    vm_net_mock_role_state before;
    u32 remaining = 0;

    if (remainingOut)
        *remainingOut = 0;
    if (role == NULL || effect == NULL || effect->itemId != itemId ||
        !vm_net_mock_role_item_effect_is_valid(effect))
    {
        return false;
    }
    memset(&active, 0, sizeof(active));
    if (!vm_net_mock_role_get_active_timed_item_effect(role, effect->kind, &active))
        return false;
    if (active.expiresUnix != 0)
    {
        printf("[info][network] mock_special_item_rejected_active account=%s role=%u kind=%u active_item=%u active_until=%u requested_item=%u\n",
               g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
               role->roleId, effect->kind, active.itemId, active.expiresUnix,
               itemId);
        return false;
    }

    before = *role;
    if (!vm_net_mock_role_consume_backpack_item(role, itemId, seq, 1, &remaining))
        return false;
    if (!vm_net_mock_role_db_save_relational(
            reason ? reason : "special-item-use", NULL, NULL, 0, false, effect, NULL))
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

static void vm_net_mock_role_db_load(void)
{
    char path[128];
    u8 fileBuf[sizeof(vm_net_mock_role_db_file)];
    vm_net_mock_role_db_file loaded;
    vm_net_mock_role_db_file_v5 equipmentInstanceLegacyFile;
    vm_net_mock_role_db_file_v4 shopWcoinFile;
    vm_net_mock_role_db_file_v3 equippedBackpackFile;
    vm_net_mock_role_db_file_v2 backpackFile;
    vm_net_mock_role_db_file_v1 legacy;
    bool loadedFromFile = false;
    bool loadedFromMysql = false;
    bool loadedFromPayload = false;
    bool mysqlEquipmentInstanceBackfill = false;
    bool mysqlExpCurveMigration = false;
    bool payloadExpCurveMigration = false;
    bool expCurveMigrationNeeded = false;
    bool needsSave = false;
    u32 migratedOldIds[VM_NET_MOCK_ROLE_DB_MAX_ROLES];
    u32 migratedNewIds[VM_NET_MOCK_ROLE_DB_MAX_ROLES];
    u32 migratedIdCount = 0;
    u32 backpackNamespaceMigratedRoles = 0;
    u32 backpackNamespaceRemappedRows = 0;
    u32 backpackNormalizedRoles = 0;
    u32 expCurveMigratedRoles = 0;
    u32 expCurveCappedRoles = 0;

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
    if (!vm_net_mock_role_db_load_mysql_relational(&loadedFromMysql,
                                                    &mysqlEquipmentInstanceBackfill,
                                                    &mysqlExpCurveMigration))
    {
        vm_autotest_note("mock_role_db_mysql_load_failed account=%s storage=relational error=%s\n",
                         g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
                         vm_mysql_last_error());
        g_vm_net_mock_role_db_valid = false;
        return;
    }
    if (!loadedFromMysql && !vm_mock_service_mysql_authority_is_sealed() &&
        !vm_net_mock_role_db_load_mysql_payload_backup(&loadedFromPayload,
                                                        &payloadExpCurveMigration))
    {
        vm_autotest_note("mock_role_db_mysql_load_failed account=%s storage=payload-backup error=%s\n",
                         g_vm_mock_service_active_account_id ? g_vm_mock_service_active_account_id : "-",
                         vm_mysql_last_error());
        g_vm_net_mock_role_db_valid = false;
        return;
    }

    if (loadedFromMysql && mysqlEquipmentInstanceBackfill)
    {
        needsSave = true;
        vm_autotest_note("mock_role_db_migrate version=5->7 source=relational account=%s\n",
                         g_vm_mock_service_active_account_id ?
                         g_vm_mock_service_active_account_id : "-");
    }
    if (loadedFromMysql && mysqlExpCurveMigration)
        expCurveMigrationNeeded = true;
    if (loadedFromPayload && payloadExpCurveMigration)
        expCurveMigrationNeeded = true;

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
            (loaded.version == 6 || loaded.version == VM_NET_MOCK_ROLE_DB_VERSION) &&
            loaded.roleCount <= VM_NET_MOCK_ROLE_DB_MAX_ROLES)
        {
            if (loaded.version < VM_NET_MOCK_ROLE_DB_VERSION)
                expCurveMigrationNeeded = true;
            g_vm_net_mock_role_db = loaded;
            g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
            loadedFromFile = true;
            if (expCurveMigrationNeeded)
                needsSave = true;
        }
        else if (readLen == sizeof(equipmentInstanceLegacyFile))
        {
            memcpy(&equipmentInstanceLegacyFile, fileBuf,
                   sizeof(equipmentInstanceLegacyFile));
            if (memcmp(equipmentInstanceLegacyFile.magic, "JHR1", 4) == 0 &&
                equipmentInstanceLegacyFile.version == 5 &&
                equipmentInstanceLegacyFile.roleCount <= VM_NET_MOCK_ROLE_DB_MAX_ROLES)
            {
                memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
                memcpy(g_vm_net_mock_role_db.magic, "JHR1", 4);
                g_vm_net_mock_role_db.version = VM_NET_MOCK_ROLE_DB_VERSION;
                g_vm_net_mock_role_db.activeRoleId =
                    equipmentInstanceLegacyFile.activeRoleId;
                g_vm_net_mock_role_db.roleCount =
                    equipmentInstanceLegacyFile.roleCount;
                for (u32 i = 0; i < equipmentInstanceLegacyFile.roleCount; ++i)
                    vm_net_mock_role_copy_from_v5(&g_vm_net_mock_role_db.roles[i],
                                                  &equipmentInstanceLegacyFile.roles[i]);
                loadedFromFile = true;
                needsSave = true;
                expCurveMigrationNeeded = true;
                vm_autotest_note("mock_role_db_migrate version=5->7 source=file roles=%u active=%u\n",
                                 g_vm_net_mock_role_db.roleCount,
                                 g_vm_net_mock_role_db.activeRoleId);
            }
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
                expCurveMigrationNeeded = true;
                vm_autotest_note("mock_role_db_migrate version=3->7 roles=%u active=%u\n",
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
                expCurveMigrationNeeded = true;
                vm_autotest_note("mock_role_db_migrate version=4->7 roles=%u active=%u\n",
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
                expCurveMigrationNeeded = true;
                vm_autotest_note("mock_role_db_migrate version=2->7 roles=%u active=%u\n",
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
                expCurveMigrationNeeded = true;
                vm_autotest_note("mock_role_db_migrate version=1->7 roles=%u active=%u\n",
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
    if (expCurveMigrationNeeded)
    {
        for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
        {
            vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
            u32 oldLevel = 0;
            u32 oldExp = role->exp;

            (void)vm_net_mock_role_migrate_exp_curve(role, &oldLevel);
            if (oldExp > vm_net_mock_role_exp_cap())
                ++expCurveCappedRoles;
            ++expCurveMigratedRoles;
        }
        /* The format marker is only advanced in the same transaction that
         * writes the remapped cumulative EXP values.  A failed save therefore
         * retries safely on the next load instead of partially migrating an
         * account. */
        needsSave = true;
        vm_autotest_note("mock_role_exp_curve_migrate version=6->7 account=%s roles=%u exp_capped=%u strategy=preserve-cumulative-exp level49_cost=2000000 level70_cost=200000000 cap_exp=%u\n",
                         g_vm_mock_service_active_account_id ?
                         g_vm_mock_service_active_account_id : "-",
                         expCurveMigratedRoles, expCurveCappedRoles,
                         vm_net_mock_role_exp_cap());
    }
    for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
    {
        vm_net_mock_role_state *role = &g_vm_net_mock_role_db.roles[i];
        u32 reservedSequenceCount =
            vm_net_mock_role_count_reserved_backpack_sequences(role);
        vm_net_mock_role_state beforeNormalize = *role;

        vm_net_mock_role_normalize(role);
        if (memcmp(&beforeNormalize, role, sizeof(beforeNormalize)) != 0)
        {
            /* This includes the `item.dsh` stack-limit repair (for example
             * a persisted 黄金宝箱 x120 becomes x99 + x21).  The relational
             * writer below deletes and replaces the account's backpack in
             * one transaction, so setting needsSave here cannot leave the
             * old row deleted without its split replacements. */
            needsSave = true;
            ++backpackNormalizedRoles;
        }
        if (reservedSequenceCount != 0)
        {
            /* Persist this exact namespace repair now.  Leaving it only in
             * memory would reintroduce the client-side sequence collision on
             * the next server process and makes the 29/3 update ambiguous. */
            needsSave = true;
            ++backpackNamespaceMigratedRoles;
            backpackNamespaceRemappedRows += reservedSequenceCount;
        }
    }
    if (backpackNamespaceMigratedRoles != 0)
    {
        printf("[info][mock-service] backpack_sequence_namespace_migrate roles=%u remapped=%u reserved=1..%u\n",
               backpackNamespaceMigratedRoles, backpackNamespaceRemappedRows,
               VM_NET_MOCK_EQUIP_SLOT_COUNT);
    }
    if (backpackNormalizedRoles != 0)
    {
        printf("[info][mock-service] backpack_normalize_persist roles=%u account=%s source=role-load-relational\n",
               backpackNormalizedRoles,
               g_vm_mock_service_active_account_id ?
                   g_vm_mock_service_active_account_id : "-");
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
                                                 migratedIdCount, true, NULL, NULL))
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
    u32 wallet_balance = 0;

    /* Refresh the compatibility cache at a low-frequency display boundary.
     * A payment callback can be handled after the role record was loaded, so
     * returning only the old role-shaped cache would temporarily reintroduce
     * different balances between two role sessions of one account. */
    if (role != NULL && g_vm_mock_account_wcoin_wallet_migrated &&
        vm_mock_service_account_wallet_read(g_vm_mock_service_active_account_id,
                                            false, &wallet_balance))
    {
        vm_mock_service_account_wallet_sync_cached_balance(
            g_vm_mock_service_active_account_id, wallet_balance);
        return wallet_balance;
    }
    return role ? role->wcoin : 0;
}

static void vm_mock_service_account_wallet_sync_cached_balance(
    const char *account_id, u32 balance)
{
    if (account_id == NULL || account_id[0] == 0)
        return;
    /* Role-management and user-web flows have already opened their account
     * database into this active cache before mutating a wallet.  Other
     * accounts intentionally stay unloaded; their next explicit acquisition
     * rehydrates from account_wallets instead of retaining a stale duplicate. */
    if (g_vm_mock_service_active_account_id != NULL &&
        strcmp(g_vm_mock_service_active_account_id, account_id) == 0)
    {
        for (u32 i = 0; i < g_vm_net_mock_role_db.roleCount; ++i)
            g_vm_net_mock_role_db.roles[i].wcoin = balance;
    }
}

static bool vm_mock_service_account_wallet_credit(const char *account_id,
                                                  u32 amount,
                                                  u32 *before_out,
                                                  u32 *after_out)
{
    char account_hex[129];
    char query[512];
    u32 before = 0;
    u32 after = 0;
    bool transaction = false;

    if (before_out)
        *before_out = 0;
    if (after_out)
        *after_out = 0;
    if (account_id == NULL || account_id[0] == 0 || amount == 0 ||
        vm_mysql_hex_encode(account_id, strlen(account_id), account_hex,
                            sizeof(account_hex)) == 0 ||
        !vm_mysql_exec("START TRANSACTION"))
    {
        return false;
    }
    transaction = true;
    if (!vm_mock_service_account_wallet_read(account_id, true, &before) ||
        before > UINT32_MAX - amount)
    {
        goto failed;
    }
    after = before + amount;
    snprintf(query, sizeof(query),
             "UPDATE account_wallets SET wcoin=%u WHERE account_id=CAST(X'%s' AS CHAR)",
             after, account_hex);
    if (!vm_mysql_exec(query) || !vm_mysql_exec("COMMIT"))
        goto failed;
    transaction = false;
    vm_mock_service_account_wallet_sync_cached_balance(account_id, after);
    if (before_out)
        *before_out = before;
    if (after_out)
        *after_out = after;
    return true;

failed:
    if (transaction)
        (void)vm_mysql_exec("ROLLBACK");
    return false;
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
                u32 previousActiveRoleId = g_vm_net_mock_role_db.activeRoleId;
                g_vm_net_mock_role_db.activeRoleId = roleId;
                if (!vm_net_mock_role_db_save("role-select"))
                {
                    g_vm_net_mock_role_db.activeRoleId = previousActiveRoleId;
                    return false;
                }
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
    if (!vm_net_mock_role_db_save_relational("role-create", NULL, NULL, 0, true, NULL, NULL))
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

    if (!vm_net_mock_role_db_save_relational("role-delete", NULL, NULL, 0, true, NULL, NULL))
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

static bool vm_net_mock_role_append_backpack_equipment_instance(
    vm_net_mock_role_state *role,
    const vm_net_mock_equipped_item_state *instance,
    u16 *seqOut)
{
    vm_net_mock_backpack_item_state *item = NULL;
    const vm_net_mock_equipment_catalog_item *catalog = NULL;
    u16 expectedMax = 0;
    u8 itemCount = 0;

    if (seqOut)
        *seqOut = 0;
    if (role == NULL || instance == NULL || instance->itemId == 0)
        return false;
    catalog = vm_net_mock_find_equipment_catalog_item(instance->itemId);
    expectedMax = vm_net_mock_equipment_durability_max_for_item(instance->itemId);
    if (catalog == NULL || expectedMax == 0)
        return false;
    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    if (itemCount >= role->backpackCapacity ||
        itemCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
    {
        return false;
    }
    item = &role->backpackItems[itemCount];
    memset(item, 0, sizeof(*item));
    item->itemId = instance->itemId;
    if (!vm_net_mock_role_allocate_backpack_sequence(
            role, NULL, 0, role->nextBackpackSeq, &item->seq))
    {
        return false;
    }
    item->enhanceLevel = (u16)SDL_min(instance->enhanceLevel,
                                      VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL);
    item->durabilityMax = expectedMax;
    item->durability = instance->durability > expectedMax
                           ? expectedMax : instance->durability;
    item->count = 1;
    role->backpackItemCount = (u8)(itemCount + 1);
    role->nextBackpackSeq = (u16)(item->seq + 1);
    if (seqOut)
        *seqOut = item->seq;
    return true;
}

/*
 * Mutate one normal backpack entry without persistence.  Multi-effect server
 * transactions (for example a shop bundle plus its W-coin charge) compose
 * several calls to this helper and commit exactly one complete role snapshot.
 * Callers that only add one item must use the persistent wrapper below.
 */
static bool vm_net_mock_role_add_backpack_item_to_role_in_memory(
    vm_net_mock_role_state *role, u32 itemId, u32 count, u16 *seqOut)
{
    const vm_net_mock_item_effect_catalog_item *effect = NULL;
    u32 reservoirCapacity = 0;
    u8 itemCount = 0;
    vm_net_mock_role_state before;

    if (seqOut)
        *seqOut = 0;
    if (role == NULL || itemId == 0 || count == 0)
        return false;

    before = *role;
    vm_net_mock_role_normalize_backpack(role);
    itemCount = vm_net_mock_role_backpack_count(role);
    effect = vm_net_mock_find_item_effect_catalog_item(itemId);
    reservoirCapacity = vm_net_mock_item_effect_reservoir_capacity(effect);
    if (reservoirCapacity != 0 || itemId == 921)
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
            if (!vm_net_mock_role_allocate_backpack_sequence(
                    role, NULL, 0, role->nextBackpackSeq, &item->seq))
            {
                *role = before;
                return false;
            }
            item->count = reservoirCapacity != 0 ? reservoirCapacity : 1;
            if (firstSeq == 0)
                firstSeq = item->seq;
            role->nextBackpackSeq = (u16)(item->seq + 1);
            role->backpackItemCount = (u8)(itemCount + unit + 1);
        }
        role->backpackItemCount = (u8)(itemCount + count);
        if (seqOut)
            *seqOut = firstSeq;
        vm_net_mock_role_normalize_backpack(role);
        return true;
    }
    if (vm_net_mock_find_equipment_catalog_item(itemId) != NULL)
    {
        vm_net_mock_equipped_item_state instance;
        u8 freeSlots = role->backpackCapacity > itemCount
                           ? (u8)(role->backpackCapacity - itemCount) : 0;
        u16 firstSeq = 0;

        if (count > freeSlots || count > VM_NET_MOCK_BACKPACK_MAX_ITEMS - itemCount)
            return false;
        memset(&instance, 0, sizeof(instance));
        instance.itemId = itemId;
        instance.durabilityMax = vm_net_mock_equipment_durability_max_for_item(itemId);
        instance.durability = instance.durabilityMax;
        if (instance.durabilityMax == 0)
            return false;
        for (u32 unit = 0; unit < count; ++unit)
        {
            u16 addedSeq = 0;
            if (!vm_net_mock_role_append_backpack_equipment_instance(
                    role, &instance, &addedSeq))
            {
                *role = before;
                return false;
            }
            if (firstSeq == 0)
                firstSeq = addedSeq;
        }
        if (seqOut)
            *seqOut = firstSeq;
        vm_net_mock_role_normalize_backpack(role);
        return true;
    }
    {
        u32 stackLimit = vm_net_mock_item_effect_stack_limit(itemId);

        if (stackLimit != 0)
        {
            u32 remaining = count;
            u16 firstSeq = 0;
            u32 newRows = 0;

            /* Fill existing partial stacks first.  `item.dsh` owns this
             * limit, not the wire's u32 count width: a 99-stack chest plus a
             * 22-stack chest must remain two sequences. */
            for (u32 i = 0; i < itemCount && remaining != 0; ++i)
            {
                vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
                u32 freeCount = 0;
                u32 added = 0;

                if (item->itemId != itemId || item->count >= stackLimit)
                    continue;
                freeCount = stackLimit - item->count;
                added = remaining < freeCount ? remaining : freeCount;
                item->count += added;
                remaining -= added;
                if (firstSeq == 0)
                    firstSeq = item->seq;
            }

            newRows = remaining / stackLimit;
            if (remaining % stackLimit != 0)
                ++newRows;
            if (newRows > role->backpackCapacity - itemCount ||
                newRows > VM_NET_MOCK_BACKPACK_MAX_ITEMS - itemCount)
            {
                *role = before;
                return false;
            }
            while (remaining != 0)
            {
                vm_net_mock_backpack_item_state *item =
                    &role->backpackItems[itemCount];
                u32 added = remaining < stackLimit ? remaining : stackLimit;

                memset(item, 0, sizeof(*item));
                item->itemId = itemId;
                if (!vm_net_mock_role_allocate_backpack_sequence(
                        role, NULL, 0, role->nextBackpackSeq, &item->seq))
                {
                    *role = before;
                    return false;
                }
                item->count = added;
                if (firstSeq == 0)
                    firstSeq = item->seq;
                role->nextBackpackSeq = (u16)(item->seq + 1);
                ++itemCount;
                role->backpackItemCount = itemCount;
                remaining -= added;
            }
            if (seqOut)
                *seqOut = firstSeq;
            vm_net_mock_role_normalize_backpack(role);
            return true;
        }
    }

    /* Unknown item resources retain the historical single-row behavior.
     * The authoritative server must not invent a stack limit when the client
     * DSH record is unavailable; the item resource issue remains observable. */
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
            return true;
        }
    }

    if (itemCount >= role->backpackCapacity || itemCount >= VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        return false;

    vm_net_mock_backpack_item_state *item = &role->backpackItems[itemCount];
    memset(item, 0, sizeof(*item));
    item->itemId = itemId;
    if (!vm_net_mock_role_allocate_backpack_sequence(
            role, NULL, 0, role->nextBackpackSeq, &item->seq))
    {
        *role = before;
        return false;
    }
    item->count = count;
    role->backpackItemCount = (u8)(itemCount + 1);
    role->nextBackpackSeq = (u16)(item->seq + 1);
    if (seqOut)
        *seqOut = item->seq;
    vm_net_mock_role_normalize_backpack(role);
    return true;
}

static bool vm_net_mock_role_add_backpack_item_to_role(vm_net_mock_role_state *role,
                                                        u32 itemId,
                                                        u32 count,
                                                        u16 *seqOut,
                                                        const char *reason)
{
    vm_net_mock_role_state before;

    if (seqOut)
        *seqOut = 0;
    if (role == NULL || itemId == 0 || count == 0)
        return false;

    before = *role;
    if (!vm_net_mock_role_add_backpack_item_to_role_in_memory(role, itemId, count, seqOut))
        return false;
    if (!vm_net_mock_role_db_save(reason ? reason : "backpack-add-item"))
    {
        *role = before;
        if (seqOut)
            *seqOut = 0;
        return false;
    }
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
    vm_net_mock_equipped_item_state selectedInstance;
    vm_net_mock_equipped_item_state oldInstance;
    vm_net_mock_role_state before;

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

    memset(&selectedInstance, 0, sizeof(selectedInstance));
    memset(&oldInstance, 0, sizeof(oldInstance));
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

    selectedInstance.itemId = itemId;
    selectedInstance.enhanceLevel = item->enhanceLevel;
    selectedInstance.durability = item->durability;
    selectedInstance.durabilityMax = item->durabilityMax;

    slot = equip->slot;
    oldInstance = role->equippedItems[slot];
    oldItemId = oldInstance.itemId;
    if (oldItemId != 0)
    {
        u8 itemCount = vm_net_mock_role_backpack_count(role);
        if (!selectedFreesSlot && itemCount >= role->backpackCapacity)
        {
            if (reasonOut)
                *reasonOut = "bag-full-for-old";
            return false;
        }
    }

    before = *role;
    if (!vm_net_mock_role_consume_backpack_item(role, itemId, seq, 1, &remaining))
    {
        if (reasonOut)
            *reasonOut = "consume-failed";
        return false;
    }
    role->equippedItems[slot] = selectedInstance;
    if (oldItemId != 0)
    {
        u16 oldSeq = 0;
        if (!vm_net_mock_role_append_backpack_equipment_instance(
                role, &oldInstance, &oldSeq))
        {
            *role = before;
            if (reasonOut)
                *reasonOut = "old-return-failed";
            return false;
        }
    }

    vm_net_mock_role_sync_derived_vitals(role);
    if (!vm_net_mock_role_db_save("item-equip"))
    {
        *role = before;
        if (reasonOut)
            *reasonOut = "persistence-failed";
        return false;
    }

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
