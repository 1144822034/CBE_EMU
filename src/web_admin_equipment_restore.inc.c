/* Restricted historical SQL importer for equipment enhancement state. */

enum
{
    VM_MOCK_ADMIN_ENHANCE_RESTORE_MAX_ROWS = 8192,
    VM_MOCK_ADMIN_ENHANCE_RESTORE_MAX_SQL = 4 * 1024 * 1024,
    VM_MOCK_ADMIN_ENHANCE_RESTORE_PREVIEW_ROWS = 120,
    VM_MOCK_ADMIN_ENHANCE_RESTORE_TOKEN_TTL_MS = 15u * 60u * 1000u,
    VM_MOCK_ADMIN_ENHANCE_RESTORE_MAX_LEVEL = 16,
    VM_MOCK_ADMIN_ENHANCE_RESTORE_MAX_AFFIX_VALUE = 0x7fff
};

typedef struct
{
    char accountId[64];
    u32 roleId;
    u32 slotIndex;
    u32 itemId;
    u32 itemSeq;
    u32 enhanceLevel;
    u32 enhanceAffixTypes;
    u64 enhanceAffixValues;
    bool backpack;
    bool found;
    u32 currentLevel;
    u32 currentAffixTypes;
    u64 currentAffixValues;
} vm_mock_admin_enhance_restore_row;

typedef struct
{
    vm_mock_admin_enhance_restore_row *rows;
    u32 count;
    u32 equipmentCount;
    u32 backpackCount;
    u32 matchedCount;
    u32 changedCount;
    u32 unchangedCount;
    u32 missingCount;
    u32 generation;
    u32 createdTick;
    bool valid;
    char sourceName[128];
    char error[192];
} vm_mock_admin_enhance_restore_state;

static vm_mock_admin_enhance_restore_state g_vm_mock_admin_enhance_restore;

static void vm_mock_admin_enhance_restore_free_state(void)
{
    free(g_vm_mock_admin_enhance_restore.rows);
    memset(&g_vm_mock_admin_enhance_restore, 0,
           sizeof(g_vm_mock_admin_enhance_restore));
}

static bool vm_mock_admin_enhance_restore_ci_equal(const char *value,
                                                    size_t valueLen,
                                                    const char *wanted)
{
    size_t wantedLen = strlen(wanted);
    return valueLen == wantedLen &&
           vm_mock_admin_ascii_ncasecmp(value, wanted, wantedLen) == 0;
}

static const char *vm_mock_admin_enhance_restore_skip_space(const char *cursor,
                                                             const char *end)
{
    while (cursor < end && isspace((unsigned char)*cursor))
        ++cursor;
    return cursor;
}

static bool vm_mock_admin_enhance_restore_keyword(const char **cursorOut,
                                                   const char *end,
                                                   const char *keyword)
{
    const char *cursor = vm_mock_admin_enhance_restore_skip_space(*cursorOut, end);
    size_t length = strlen(keyword);

    if ((size_t)(end - cursor) < length ||
        vm_mock_admin_ascii_ncasecmp(cursor, keyword, length) != 0)
        return false;
    if (cursor + length < end &&
        (isalnum((unsigned char)cursor[length]) || cursor[length] == '_'))
        return false;
    *cursorOut = cursor + length;
    return true;
}

static bool vm_mock_admin_enhance_restore_identifier(const char **cursorOut,
                                                      const char *end,
                                                      char *out,
                                                      size_t outCap)
{
    const char *cursor = vm_mock_admin_enhance_restore_skip_space(*cursorOut, end);
    const char *start = cursor;
    size_t length;

    if (cursor >= end)
        return false;
    if (*cursor == '`')
    {
        start = ++cursor;
        while (cursor < end && *cursor != '`')
            ++cursor;
        if (cursor >= end)
            return false;
        length = (size_t)(cursor - start);
        ++cursor;
    }
    else
    {
        while (cursor < end && (isalnum((unsigned char)*cursor) ||
                                *cursor == '_' || *cursor == '$'))
            ++cursor;
        length = (size_t)(cursor - start);
    }
    if (length == 0 || length >= outCap)
        return false;
    memcpy(out, start, length);
    out[length] = 0;
    *cursorOut = cursor;
    return true;
}

static bool vm_mock_admin_enhance_restore_number(const char *value,
                                                  size_t valueLen,
                                                  u64 maximum,
                                                  u64 *numberOut)
{
    char text[48];
    size_t begin = 0;
    size_t end = valueLen;
    u64 number = 0;

    while (begin < end && isspace((unsigned char)value[begin]))
        ++begin;
    while (end > begin && isspace((unsigned char)value[end - 1]))
        --end;
    if (end <= begin || end - begin >= sizeof(text))
        return false;
    memcpy(text, value + begin, end - begin);
    text[end - begin] = 0;
    for (size_t i = 0; text[i] != 0; ++i)
    {
        if (!isdigit((unsigned char)text[i]))
            return false;
        if (number > (UINT64_MAX - (u64)(text[i] - '0')) / 10u)
            return false;
        number = number * 10u + (u64)(text[i] - '0');
    }
    if (number > maximum)
        return false;
    *numberOut = number;
    return true;
}

static bool vm_mock_admin_enhance_restore_sql_value(const char **cursorOut,
                                                     const char *end,
                                                     char *out,
                                                     size_t outCap,
                                                     size_t *lengthOut)
{
    const char *cursor = vm_mock_admin_enhance_restore_skip_space(*cursorOut, end);
    size_t length = 0;

    if (cursor >= end)
        return false;
    if (*cursor == '\'')
    {
        ++cursor;
        while (cursor < end)
        {
            char ch = *cursor++;
            if (ch == '\'')
            {
                if (cursor < end && *cursor == '\'')
                {
                    ++cursor;
                    ch = '\'';
                }
                else
                    break;
            }
            else if (ch == '\\' && cursor < end)
            {
                char escaped = *cursor++;
                if (escaped == 'n') ch = '\n';
                else if (escaped == 'r') ch = '\r';
                else if (escaped == 't') ch = '\t';
                else ch = escaped;
            }
            if (length + 1 >= outCap)
                return false;
            out[length++] = ch;
        }
        if (cursor > end || (cursor == end && end[-1] != '\''))
            return false;
    }
    else
    {
        const char *start = cursor;
        while (cursor < end && *cursor != ',' && *cursor != ')')
            ++cursor;
        while (cursor > start && isspace((unsigned char)cursor[-1]))
            --cursor;
        if (cursor == start || (size_t)(cursor - start) >= outCap)
            return false;
        memcpy(out, start, (size_t)(cursor - start));
        length = (size_t)(cursor - start);
        if (vm_mock_admin_enhance_restore_ci_equal(out, length, "NULL"))
            return false;
    }
    out[length] = 0;
    *cursorOut = vm_mock_admin_enhance_restore_skip_space(cursor, end);
    if (lengthOut != NULL)
        *lengthOut = length;
    return true;
}

static bool vm_mock_admin_enhance_restore_primary_key_equal(
    const vm_mock_admin_enhance_restore_row *left,
    const vm_mock_admin_enhance_restore_row *right)
{
    return left->backpack == right->backpack &&
           strcmp(left->accountId, right->accountId) == 0 &&
           left->roleId == right->roleId && left->slotIndex == right->slotIndex;
}

static bool vm_mock_admin_enhance_restore_row_changed(
    const vm_mock_admin_enhance_restore_row *row)
{
    return row != NULL && row->found &&
           (row->currentLevel != row->enhanceLevel ||
            row->currentAffixTypes != row->enhanceAffixTypes ||
            row->currentAffixValues != row->enhanceAffixValues);
}

static bool vm_mock_admin_enhance_restore_affixes_valid(u32 types, u64 values)
{
    u32 seen = 0;

    for (u32 stage = 0; stage < 4; ++stage)
    {
        u32 type = (types >> (stage * 8u)) & 0xffu;
        u32 value = (u32)((values >> (stage * 16u)) & 0xffffu);

        if (type == 0)
        {
            if (value != 0)
                return false;
            continue;
        }
        /* These are the only four-stage attribute identifiers emitted by the
         * catalogue (wisdom, attack, armour, dodge, hit, crit, HP, MP). */
        if (type < 3 || type > 10 || value == 0 ||
            value > VM_MOCK_ADMIN_ENHANCE_RESTORE_MAX_AFFIX_VALUE ||
            (seen & (1u << type)) != 0)
            return false;
        seen |= 1u << type;
    }
    return true;
}

static bool vm_mock_admin_enhance_restore_token_valid(u32 token)
{
    u32 now;

    if (!g_vm_mock_admin_enhance_restore.valid ||
        token == 0 || token != g_vm_mock_admin_enhance_restore.generation)
        return false;
    now = scheduler_get_tick_ms();
    if ((u32)(now - g_vm_mock_admin_enhance_restore.createdTick) >
        VM_MOCK_ADMIN_ENHANCE_RESTORE_TOKEN_TTL_MS)
    {
        vm_mock_admin_enhance_restore_free_state();
        return false;
    }
    return true;
}

/* multipart form readers intentionally expose the uploaded SQL as a text
 * value, but the hidden `sql_name` field is only a UI fallback.  Capture the
 * browser-provided filename for the audit row without ever trusting it for a
 * filesystem path or executing anything from the upload. */
static bool vm_mock_admin_enhance_restore_multipart_filename(
    const char *form, const char *wantedField, char *out, size_t outCap)
{
    const char *name = NULL;
    const char *filename = NULL;
    const char *filenameEnd = NULL;
    const char *lineEnd = NULL;
    size_t wantedLen = wantedField ? strlen(wantedField) : 0;
    size_t filenameLen = 0;

    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (form == NULL || wantedLen == 0 || strncmp(form, "--", 2) != 0)
        return false;
    name = form;
    while ((name = strstr(name, "name=\"")) != NULL)
    {
        const char *nameValue = name + 6;
        const char *nameEnd = strchr(nameValue, '"');

        if (nameEnd == NULL)
            return false;
        if ((size_t)(nameEnd - nameValue) == wantedLen &&
            memcmp(nameValue, wantedField, wantedLen) == 0)
        {
            lineEnd = strstr(nameEnd, "\r\n\r\n");
            if (lineEnd == NULL)
                return false;
            filename = strstr(nameEnd, "filename=\"");
            if (filename == NULL || filename > lineEnd)
                return false;
            filename += 10;
            filenameEnd = strchr(filename, '"');
            if (filenameEnd == NULL || filenameEnd > lineEnd)
                return false;
            filenameLen = (size_t)(filenameEnd - filename);
            while (filenameLen != 0 &&
                   (filename[filenameLen - 1] == '/' ||
                    filename[filenameLen - 1] == '\\'))
                --filenameLen;
            if (filenameLen == 0 || filenameLen >= outCap)
                return false;
            for (size_t i = 0; i < filenameLen; ++i)
                if (filename[i] == '\r' || filename[i] == '\n')
                    return false;
            memcpy(out, filename, filenameLen);
            out[filenameLen] = 0;
            /* Browsers may submit a full local path; retain only the leaf. */
            {
                char *slash = strrchr(out, '/');
                char *backslash = strrchr(out, '\\');
                char *leaf = slash != NULL ? slash : backslash;
                if (backslash != NULL &&
                    (leaf == NULL || backslash > leaf))
                    leaf = backslash;
                if (leaf != NULL && leaf[1] != 0)
                    memmove(out, leaf + 1, strlen(leaf + 1) + 1);
            }
            return out[0] != 0;
        }
        name = nameEnd + 1;
    }
    return false;
}

static bool vm_mock_admin_enhance_restore_parse_table(
    const char **cursorOut, const char *end, const char *table,
    vm_mock_admin_enhance_restore_state *state)
{
    const char *cursor = *cursorOut;
    char columns[16][40];
    u32 columnCount = 0;
    u32 required[10];
    u32 requiredCount = 0;
    bool backpack = strcmp(table, "account_role_backpack") == 0;

    cursor = vm_mock_admin_enhance_restore_skip_space(cursor, end);
    if (vm_mock_admin_enhance_restore_keyword(&cursor, end, "VALUES"))
    {
        static const char *equipmentColumns[] = {
            "account_id", "role_id", "slot_index", "item_id",
            "enhance_level", "enhance_affix_types", "enhance_affix_values",
            "durability", "durability_max"};
        static const char *backpackColumns[] = {
            "account_id", "role_id", "slot_index", "item_id", "item_seq",
            "item_count", "enhance_level", "enhance_affix_types",
            "enhance_affix_values", "durability", "durability_max"};
        const char *const *defaults = backpack ? backpackColumns : equipmentColumns;
        columnCount = backpack ? 11 : 9;
        for (u32 i = 0; i < columnCount; ++i)
            snprintf(columns[i], sizeof(columns[i]), "%s", defaults[i]);
    }
    else
    {
        if (cursor >= end || *cursor != '(')
            return false;
        ++cursor;
        while (cursor < end && *cursor != ')')
        {
            if (columnCount >= 16 ||
                !vm_mock_admin_enhance_restore_identifier(&cursor, end,
                                                          columns[columnCount],
                                                          sizeof(columns[0])))
                return false;
            ++columnCount;
            cursor = vm_mock_admin_enhance_restore_skip_space(cursor, end);
            if (cursor < end && *cursor == ',') ++cursor;
            else if (cursor >= end || *cursor != ')') return false;
        }
        if (cursor >= end || *cursor != ')' || columnCount == 0)
            return false;
        ++cursor;
        cursor = vm_mock_admin_enhance_restore_skip_space(cursor, end);
        if (!vm_mock_admin_enhance_restore_keyword(&cursor, end, "VALUES"))
            return false;
    }

    for (u32 i = 0; i < columnCount; ++i)
    {
        if (vm_mock_admin_enhance_restore_ci_equal(columns[i], strlen(columns[i]), "account_id")) required[0] = i;
        else if (vm_mock_admin_enhance_restore_ci_equal(columns[i], strlen(columns[i]), "role_id")) required[1] = i;
        else if (vm_mock_admin_enhance_restore_ci_equal(columns[i], strlen(columns[i]), "slot_index")) required[2] = i;
        else if (vm_mock_admin_enhance_restore_ci_equal(columns[i], strlen(columns[i]), "item_id")) required[3] = i;
        else if (vm_mock_admin_enhance_restore_ci_equal(columns[i], strlen(columns[i]), "item_seq")) required[4] = i;
        else if (vm_mock_admin_enhance_restore_ci_equal(columns[i], strlen(columns[i]), "enhance_level")) required[5] = i;
        else if (vm_mock_admin_enhance_restore_ci_equal(columns[i], strlen(columns[i]), "enhance_affix_types")) required[6] = i;
        else if (vm_mock_admin_enhance_restore_ci_equal(columns[i], strlen(columns[i]), "enhance_affix_values")) required[7] = i;
    }
    requiredCount = backpack ? 8 : 8;
    for (u32 i = 0; i < requiredCount; ++i)
    {
        if (i == 4 && !backpack) continue;
        bool present = false;
        for (u32 j = 0; j < columnCount; ++j)
        {
            const char *wanted = i == 0 ? "account_id" : i == 1 ? "role_id" :
                i == 2 ? "slot_index" : i == 3 ? "item_id" : i == 4 ? "item_seq" :
                i == 5 ? "enhance_level" : i == 6 ? "enhance_affix_types" : "enhance_affix_values";
            if (vm_mock_admin_enhance_restore_ci_equal(columns[j], strlen(columns[j]), wanted)) { present = true; break; }
        }
        if (!present) return false;
    }

    while (cursor < end)
    {
        char values[16][256];
        size_t lengths[16];
        vm_mock_admin_enhance_restore_row row;
        u64 number;

        cursor = vm_mock_admin_enhance_restore_skip_space(cursor, end);
        if (cursor >= end || *cursor == ';') break;
        if (*cursor != '(') return false;
        ++cursor;
        memset(&row, 0, sizeof(row));
        row.backpack = backpack;
        for (u32 i = 0; i < columnCount; ++i)
        {
            if (!vm_mock_admin_enhance_restore_sql_value(&cursor, end, values[i], sizeof(values[i]), &lengths[i])) return false;
            if (i + 1 < columnCount)
            {
                if (cursor >= end || *cursor != ',') return false;
                ++cursor;
            }
            else if (cursor >= end || *cursor != ')') return false;
            if (i + 1 == columnCount) ++cursor;
        }
        if (lengths[required[0]] >= sizeof(row.accountId)) return false;
        memcpy(row.accountId, values[required[0]], lengths[required[0]]); row.accountId[lengths[required[0]]] = 0;
        if (!vm_mock_admin_enhance_restore_number(values[required[1]], lengths[required[1]], UINT32_MAX, &number)) return false; row.roleId = (u32)number;
        if (!vm_mock_admin_enhance_restore_number(values[required[2]], lengths[required[2]], UINT32_MAX, &number)) return false; row.slotIndex = (u32)number;
        if (!vm_mock_admin_enhance_restore_number(values[required[3]], lengths[required[3]], UINT32_MAX, &number)) return false; row.itemId = (u32)number;
        if (backpack) { if (!vm_mock_admin_enhance_restore_number(values[required[4]], lengths[required[4]], 65535, &number)) return false; row.itemSeq = (u32)number; }
        if (!vm_mock_admin_enhance_restore_number(values[required[5]], lengths[required[5]], VM_MOCK_ADMIN_ENHANCE_RESTORE_MAX_LEVEL, &number)) return false; row.enhanceLevel = (u32)number;
        if (!vm_mock_admin_enhance_restore_number(values[required[6]], lengths[required[6]], UINT32_MAX, &number)) return false; row.enhanceAffixTypes = (u32)number;
        if (!vm_mock_admin_enhance_restore_number(values[required[7]], lengths[required[7]], UINT64_MAX, &row.enhanceAffixValues) ||
            !vm_mock_admin_enhance_restore_affixes_valid(row.enhanceAffixTypes, row.enhanceAffixValues)) return false;
        if (row.accountId[0] == 0 || row.roleId == 0 || row.itemId == 0 || state->count >= VM_MOCK_ADMIN_ENHANCE_RESTORE_MAX_ROWS) return false;
        for (u32 i = 0; i < state->count; ++i)
        {
            if (vm_mock_admin_enhance_restore_primary_key_equal(&state->rows[i], &row))
                return false;
        }
        state->rows[state->count++] = row;
        if (backpack) ++state->backpackCount; else ++state->equipmentCount;
        cursor = vm_mock_admin_enhance_restore_skip_space(cursor, end);
        if (cursor < end && *cursor == ',') { ++cursor; continue; }
        if (cursor < end && *cursor == ';') break;
        if (cursor < end && *cursor == '/') continue;
        if (cursor < end && *cursor != ';') return false;
    }
    *cursorOut = cursor;
    return true;
}

static bool vm_mock_admin_enhance_restore_parse(const char *sql, size_t sqlLen,
                                                 vm_mock_admin_enhance_restore_state *state)
{
    const char *cursor = sql;
    const char *end = sql + sqlLen;
    bool foundTable = false;

    while (cursor < end)
    {
        const char *insert = cursor;
        while (insert + 6 <= end && vm_mock_admin_ascii_ncasecmp(insert, "INSERT", 6) != 0) ++insert;
        if (insert + 6 > end) break;
        cursor = insert + 6;
        if (!vm_mock_admin_enhance_restore_keyword(&cursor, end, "INTO")) { cursor = insert + 6; continue; }
        char table[80];
        if (!vm_mock_admin_enhance_restore_identifier(&cursor, end, table, sizeof(table))) return false;
        if (cursor < end && *vm_mock_admin_enhance_restore_skip_space(cursor, end) == '.')
        {
            cursor = vm_mock_admin_enhance_restore_skip_space(cursor, end) + 1;
            if (!vm_mock_admin_enhance_restore_identifier(&cursor, end, table, sizeof(table))) return false;
        }
        if (!vm_mock_admin_enhance_restore_ci_equal(table, strlen(table), "account_role_equipment") &&
            !vm_mock_admin_enhance_restore_ci_equal(table, strlen(table), "account_role_backpack")) { cursor = insert + 6; continue; }
        foundTable = true;
        if (!vm_mock_admin_enhance_restore_parse_table(&cursor, end,
                                                        vm_mock_admin_enhance_restore_ci_equal(table, strlen(table), "account_role_backpack") ? "account_role_backpack" : "account_role_equipment",
                                                        state)) return false;
    }
    return foundTable && state->count != 0;
}

typedef struct { u32 level; u32 types; u64 values; bool found; } vm_mock_admin_enhance_restore_db_row;

static bool vm_mock_admin_enhance_restore_db_row_cb(void *context, unsigned int count,
                                                     const char *const *values,
                                                     const size_t *lengths)
{
    vm_mock_admin_enhance_restore_db_row *row = context;
    u64 number;
    if (count != 3 || !vm_mock_admin_enhance_restore_number(values[0], lengths[0], 65535, &number)) return false;
    row->level = (u32)number;
    if (!vm_mock_admin_enhance_restore_number(values[1], lengths[1], UINT32_MAX, &number)) return false;
    row->types = (u32)number;
    if (!vm_mock_admin_enhance_restore_number(values[2], lengths[2], UINT64_MAX, &row->values)) return false;
    row->found = true;
    return true;
}

static bool vm_mock_admin_enhance_restore_lookup(
    vm_mock_admin_enhance_restore_row *row, bool forUpdate)
{
    char hex[sizeof(row->accountId) * 2 + 1];
    char sql[512];
    vm_mock_admin_enhance_restore_db_row current;
    size_t accountLen = strlen(row->accountId);
    if (vm_mysql_hex_encode(row->accountId, accountLen, hex, sizeof(hex)) == 0) return false;
    memset(&current, 0, sizeof(current));
    if (row->backpack)
        snprintf(sql, sizeof(sql), "SELECT enhance_level,enhance_affix_types,enhance_affix_values FROM account_role_backpack WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND slot_index=%u AND item_id=%u AND item_seq=%u%s", hex, row->roleId, row->slotIndex, row->itemId, row->itemSeq, forUpdate ? " FOR UPDATE" : "");
    else
        snprintf(sql, sizeof(sql), "SELECT enhance_level,enhance_affix_types,enhance_affix_values FROM account_role_equipment WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND slot_index=%u AND item_id=%u%s", hex, row->roleId, row->slotIndex, row->itemId, forUpdate ? " FOR UPDATE" : "");
    if (!vm_mysql_query(sql, vm_mock_admin_enhance_restore_db_row_cb, &current)) return false;
    row->found = current.found;
    row->currentLevel = current.level;
    row->currentAffixTypes = current.types;
    row->currentAffixValues = current.values;
    return true;
}

static bool vm_mock_admin_enhance_restore_prepare(vm_mock_admin_enhance_restore_state *state)
{
    for (u32 i = 0; i < state->count; ++i)
    {
        if (vm_mock_admin_account_is_online(state->rows[i].accountId))
        {
            snprintf(state->error, sizeof(state->error),
                     "账号 %s 当前有在线角色，请先退出游戏后再恢复",
                     state->rows[i].accountId);
            return false;
        }
        if (!vm_mock_admin_enhance_restore_lookup(&state->rows[i], false)) return false;
        if (!state->rows[i].found) ++state->missingCount;
        else
        {
            vm_mock_admin_enhance_restore_row *row = &state->rows[i];
            ++state->matchedCount;
            if (vm_mock_admin_enhance_restore_row_changed(row))
                ++state->changedCount;
            else
                ++state->unchangedCount;
        }
    }
    return true;
}

static bool vm_mock_admin_enhance_restore_ensure_audit_table(void)
{
    return vm_mysql_exec(
        "CREATE TABLE IF NOT EXISTS server_admin_enhancement_restore_audit ("
        "audit_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
        "source_name VARCHAR(127) NOT NULL,source_row_count INT UNSIGNED NOT NULL,"
        "matched_row_count INT UNSIGNED NOT NULL,changed_row_count INT UNSIGNED NOT NULL,"
        "missing_row_count INT UNSIGNED NOT NULL,created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "PRIMARY KEY(audit_id)) ENGINE=InnoDB");
}

static void vm_mock_admin_render_enhance_restore_page(char *response, size_t responseCap,
                                                       const char *query)
{
    vm_mock_admin_text page;
    char status[16]; char message[256]; char token[32];
    u32 tokenValue = 0;
    vm_mock_admin_text_init(&page, response, responseCap);
    memset(status, 0, sizeof(status)); memset(message, 0, sizeof(message)); memset(token, 0, sizeof(token));
    (void)vm_mock_admin_form_value(query, "status", status, sizeof(status));
    (void)vm_mock_admin_form_value(query, "message", message, sizeof(message));
    (void)vm_mock_admin_form_value(query, "restore_token", token, sizeof(token));
    (void)vm_net_mock_parse_u32_strict(token, &tokenValue);
    vm_mock_admin_text_appendf(&page, "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>装备强化恢复 · 江湖OL 后台</title><style>body{font-family:system-ui;margin:24px;color:#20324d}.tabs{display:flex;gap:8px;flex-wrap:wrap}.tab{padding:8px 12px;border:1px solid #d7dfec;border-radius:7px;text-decoration:none;color:#385170}.tab.on{background:#1f62c9;color:#fff}.card{border:1px solid #d7dfec;border-radius:8px;padding:18px;margin:12px 0}.notice{padding:10px;border-radius:6px;background:#eef5ff}.error{background:#fff0f0;color:#9a1d1d}table{border-collapse:collapse;width:100%%}td,th{border:1px solid #d7dfec;padding:5px;text-align:left;font-size:12px}</style><script src=\"/admin.js\" defer></script></head><body><main class=\"wrap\"><nav class=\"tabs\"><a class=\"tab\" href=\"/?tab=accounts\">账号管理</a><a class=\"tab on\" href=\"/?tab=enhance-restore\">强化恢复</a><a class=\"tab\" href=\"/?tab=content\">游戏内容管理</a><a class=\"tab\" href=\"/?tab=tasks\">任务管理</a><a class=\"tab\" href=\"/?tab=monsters\">怪物管理</a><a class=\"tab\" href=\"/?tab=risk\">风险角色管理</a></nav><h1>装备强化数据恢复</h1><p>上传历史 SQL，仅恢复严格匹配装备实例的强化等级和强化词条；不会执行上传文件中的 SQL。</p>");
    if (status[0]) { vm_mock_admin_text_appendf(&page, "<div class=\"notice%s\">", strcmp(status, "error") == 0 ? " error" : ""); vm_mock_admin_text_append_html(&page, message); vm_mock_admin_text_appendf(&page, "</div>"); }
    vm_mock_admin_text_appendf(&page, "<section class=\"card\"><form method=\"post\" action=\"/action\" enctype=\"multipart/form-data\"><input type=\"hidden\" name=\"action\" value=\"restore-equipment-preview\"><label>历史 SQL 文件 <input type=\"file\" name=\"sql_file\" accept=\".sql,text/sql\" required></label><input type=\"hidden\" name=\"sql_name\" value=\"historical-backup.sql\"><button type=\"submit\">解析并预览</button></form></section>");
    if (vm_mock_admin_enhance_restore_token_valid(tokenValue))
    {
        vm_mock_admin_text_appendf(&page, "<section class=\"card\"><h2>预览</h2><p>解析 %u 行：装备栏 %u，背包 %u；精确匹配 %u，待覆盖 %u，数据相同跳过 %u，不匹配 %u。</p><form method=\"post\" action=\"/action\"><input type=\"hidden\" name=\"action\" value=\"restore-equipment-commit\"><input type=\"hidden\" name=\"restore_token\" value=\"%u\"><button type=\"submit\">确认覆盖强化数据</button></form><table><tr><th>表</th><th>账号/角色/槽位</th><th>物品</th><th>等级</th><th>词条类型</th><th>词条数值</th><th>状态</th></tr>", g_vm_mock_admin_enhance_restore.count, g_vm_mock_admin_enhance_restore.equipmentCount, g_vm_mock_admin_enhance_restore.backpackCount, g_vm_mock_admin_enhance_restore.matchedCount, g_vm_mock_admin_enhance_restore.changedCount, g_vm_mock_admin_enhance_restore.unchangedCount, g_vm_mock_admin_enhance_restore.missingCount, g_vm_mock_admin_enhance_restore.generation);
        u32 shown = 0;
        for (u32 i = 0; i < g_vm_mock_admin_enhance_restore.count && shown < VM_MOCK_ADMIN_ENHANCE_RESTORE_PREVIEW_ROWS; ++i)
        {
            vm_mock_admin_enhance_restore_row *row = &g_vm_mock_admin_enhance_restore.rows[i];
            const char *rowStatus = !row->found ? "不匹配" : vm_mock_admin_enhance_restore_row_changed(row) ? "覆盖历史值" : "相同，跳过";
            vm_mock_admin_text_appendf(&page, "<tr><td>%s</td><td>", row->backpack ? "背包" : "装备栏"); vm_mock_admin_text_append_html(&page, row->accountId); vm_mock_admin_text_appendf(&page, " / %u / %u</td><td>%u%s</td><td>%s%u → %u</td><td>%u → %u</td><td>%llu → %llu</td><td>%s</td></tr>", row->roleId, row->slotIndex, row->itemId, row->backpack ? "（含实例序号匹配）" : "", row->found ? "" : "-", row->found ? row->currentLevel : 0, row->enhanceLevel, row->currentAffixTypes, row->enhanceAffixTypes, (unsigned long long)row->currentAffixValues, (unsigned long long)row->enhanceAffixValues, rowStatus);
            ++shown;
        }
        vm_mock_admin_text_appendf(&page, "</table></section>");
    }
    vm_mock_admin_text_appendf(&page, "</main></body></html>");
}

static void vm_mock_admin_redirect_enhance_restore(vm_mock_service_socket client,
                                                    const char *status,
                                                    const char *message,
                                                    u32 token)
{
    char encoded[768]; char location[1400];
    vm_mock_admin_url_encode(status ? status : "error", encoded, sizeof(encoded));
    snprintf(location, sizeof(location), VM_MOCK_ADMIN_ROOT_PATH "?tab=enhance-restore&restore_token=%u&status=%s&message=", token, encoded);
    vm_mock_admin_url_encode(message ? message : "操作失败", encoded, sizeof(encoded));
    strncat(location, encoded, sizeof(location) - strlen(location) - 1);
    { char headers[1500]; snprintf(headers, sizeof(headers), "Location: %s\r\n", location); vm_mock_admin_send_response(client, "303 See Other", "text/plain; charset=utf-8", headers, "正在返回后台页面。\n"); }
}

static void vm_mock_admin_handle_enhance_restore_action(vm_mock_service_socket client,
                                                         const char *body)
{
    char action[48];
    memset(action, 0, sizeof(action));
    if (!vm_mock_admin_form_value(body, "action", action, sizeof(action))) { vm_mock_admin_redirect_enhance_restore(client, "error", "请求参数不完整", 0); return; }
    if (strcmp(action, "restore-equipment-preview") == 0)
    {
        char *sql = (char *)malloc(VM_MOCK_ADMIN_ENHANCE_RESTORE_MAX_SQL + 1);
        char sourceName[128];
        vm_mock_admin_enhance_restore_state next;
        memset(&next, 0, sizeof(next));
        memset(sourceName, 0, sizeof(sourceName));
        if (!vm_mock_admin_enhance_restore_multipart_filename(
                body, "sql_file", sourceName, sizeof(sourceName)))
            (void)vm_mock_admin_form_value(body, "sql_name", sourceName,
                                           sizeof(sourceName));
        snprintf(next.sourceName, sizeof(next.sourceName), "%s",
                 sourceName[0] ? sourceName : "historical-backup.sql");
        next.rows = (vm_mock_admin_enhance_restore_row *)calloc(VM_MOCK_ADMIN_ENHANCE_RESTORE_MAX_ROWS, sizeof(*next.rows));
        if (!sql || !next.rows || !vm_mock_admin_form_value(body, "sql_file", sql, VM_MOCK_ADMIN_ENHANCE_RESTORE_MAX_SQL + 1) || !vm_mock_admin_enhance_restore_parse(sql, strlen(sql), &next) || !vm_mock_admin_enhance_restore_prepare(&next))
        { const char *message = next.error[0] ? next.error : "SQL 文件格式无效，必须包含可解析的装备实例 INSERT 数据"; free(sql); free(next.rows); vm_mock_admin_redirect_enhance_restore(client, "error", message, 0); return; }
        free(sql); vm_mock_admin_enhance_restore_free_state(); g_vm_mock_admin_enhance_restore = next; g_vm_mock_admin_enhance_restore.valid = true; g_vm_mock_admin_enhance_restore.createdTick = scheduler_get_tick_ms(); g_vm_mock_admin_enhance_restore.generation = g_vm_mock_admin_enhance_restore.createdTick ^ (u32)(uintptr_t)&next; if (g_vm_mock_admin_enhance_restore.generation == 0) g_vm_mock_admin_enhance_restore.generation = 1;
        vm_mock_admin_redirect_enhance_restore(client, "ok", "解析成功，请核对预览后确认覆盖", g_vm_mock_admin_enhance_restore.generation); return;
    }
    if (strcmp(action, "restore-equipment-commit") == 0)
    {
        char token[32]; u32 generation = 0; bool ok = true; u32 changed = 0;
        memset(token, 0, sizeof(token));
        if (!vm_mock_admin_form_value(body, "restore_token", token, sizeof(token)) || !vm_net_mock_parse_u32_strict(token, &generation) || !vm_mock_admin_enhance_restore_token_valid(generation)) { vm_mock_admin_redirect_enhance_restore(client, "error", "恢复预览已过期，请重新上传 SQL", 0); return; }
        if (!vm_mock_admin_enhance_restore_ensure_audit_table() || !vm_mysql_exec("START TRANSACTION")) { vm_mock_admin_redirect_enhance_restore(client, "error", "无法开始恢复事务", generation); return; }
        /* Re-check immediately before any UPDATE.  Preview is deliberately
         * read-only, so an account may have logged in during the confirmation
         * window; never let that live snapshot race the database restore. */
        for (u32 i = 0; i < g_vm_mock_admin_enhance_restore.count; ++i)
        {
            if (vm_mock_admin_account_is_online(g_vm_mock_admin_enhance_restore.rows[i].accountId))
            {
                ok = false;
                break;
            }
        }
        if (!ok)
        {
            (void)vm_mysql_exec("ROLLBACK");
            vm_mock_admin_redirect_enhance_restore(client, "error", "确认期间账号已上线，恢复已取消，请退出游戏后重新预览", generation);
            return;
        }
        /* Lock and revalidate every source location before the first write.
         * This protects the preview contract from another administrative
         * change during its 15-minute confirmation window. */
        for (u32 i = 0; i < g_vm_mock_admin_enhance_restore.count; ++i)
        {
            vm_mock_admin_enhance_restore_row *row =
                &g_vm_mock_admin_enhance_restore.rows[i];
            bool expectedFound = row->found;
            u32 expectedLevel = row->currentLevel;
            u32 expectedTypes = row->currentAffixTypes;
            u64 expectedValues = row->currentAffixValues;

            if (!vm_mock_admin_enhance_restore_lookup(row, true) ||
                row->found != expectedFound ||
                (row->found &&
                 (row->currentLevel != expectedLevel ||
                  row->currentAffixTypes != expectedTypes ||
                  row->currentAffixValues != expectedValues)))
            {
                ok = false;
                break;
            }
        }
        if (!ok)
        {
            (void)vm_mysql_exec("ROLLBACK");
            vm_mock_admin_redirect_enhance_restore(client, "error", "预览后装备实例或强化数据已变化，恢复已取消，请重新上传并预览", generation);
            return;
        }
        for (u32 i = 0; i < g_vm_mock_admin_enhance_restore.count; ++i)
        {
            vm_mock_admin_enhance_restore_row *row = &g_vm_mock_admin_enhance_restore.rows[i]; char hex[129]; char sql[768];
            if (!vm_mock_admin_enhance_restore_row_changed(row)) continue;
            if (vm_mysql_hex_encode(row->accountId, strlen(row->accountId), hex, sizeof(hex)) == 0) { ok = false; break; }
            if (row->backpack) snprintf(sql, sizeof(sql), "UPDATE account_role_backpack SET enhance_level=%u,enhance_affix_types=%u,enhance_affix_values=%llu WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND slot_index=%u AND item_id=%u AND item_seq=%u", row->enhanceLevel, row->enhanceAffixTypes, (unsigned long long)row->enhanceAffixValues, hex, row->roleId, row->slotIndex, row->itemId, row->itemSeq);
            else snprintf(sql, sizeof(sql), "UPDATE account_role_equipment SET enhance_level=%u,enhance_affix_types=%u,enhance_affix_values=%llu WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND slot_index=%u AND item_id=%u", row->enhanceLevel, row->enhanceAffixTypes, (unsigned long long)row->enhanceAffixValues, hex, row->roleId, row->slotIndex, row->itemId);
            if (!vm_mysql_exec(sql)) { ok = false; break; } ++changed;
        }
        if (ok)
        {
            char nameHex[sizeof(g_vm_mock_admin_enhance_restore.sourceName) * 2 + 1];
            char auditSql[640];
            if (vm_mysql_hex_encode(g_vm_mock_admin_enhance_restore.sourceName,
                                    strlen(g_vm_mock_admin_enhance_restore.sourceName),
                                    nameHex, sizeof(nameHex)) == 0)
                ok = false;
            else
            {
                snprintf(auditSql, sizeof(auditSql), "INSERT INTO server_admin_enhancement_restore_audit(source_name,source_row_count,matched_row_count,changed_row_count,missing_row_count) VALUES(CAST(X'%s' AS CHAR),%u,%u,%u,%u)", nameHex, g_vm_mock_admin_enhance_restore.count, g_vm_mock_admin_enhance_restore.matchedCount, changed, g_vm_mock_admin_enhance_restore.missingCount);
                ok = vm_mysql_exec(auditSql);
            }
        }
        if (ok && vm_mysql_exec("COMMIT")) { char message[128]; snprintf(message, sizeof(message), "强化数据恢复完成，共覆盖 %u 个装备实例", changed); vm_mock_admin_enhance_restore_free_state(); vm_mock_admin_redirect_enhance_restore(client, "ok", message, generation); }
        else { (void)vm_mysql_exec("ROLLBACK"); vm_mock_admin_redirect_enhance_restore(client, "error", "恢复失败，已回滚全部变更", generation); }
    }
}
