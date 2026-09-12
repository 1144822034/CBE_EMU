/*
 * Physical gateway zone catalog for Pixi / host-layer multi-zone.
 * Separate from title WT 1/1/12 server_login_servers (display-only).
 * See docs/re/2026-09-10-gateway-zone-catalog.md.
 */

#define VM_NET_MOCK_GATEWAY_ZONE_MAX 16
#define VM_NET_MOCK_GATEWAY_ZONE_NAME_CAP 64
#define VM_NET_MOCK_GATEWAY_ZONE_LABEL_CAP 64
#define VM_NET_MOCK_GATEWAY_ZONE_WS_CAP 192
#define VM_NET_MOCK_GATEWAY_ZONE_WEB_CAP 192

typedef struct
{
    u32 zoneId;
    char displayName[VM_NET_MOCK_GATEWAY_ZONE_NAME_CAP];
    char label[VM_NET_MOCK_GATEWAY_ZONE_LABEL_CAP];
    char wsUrl[VM_NET_MOCK_GATEWAY_ZONE_WS_CAP];
    char accountWebUrl[VM_NET_MOCK_GATEWAY_ZONE_WEB_CAP];
    u32 sortOrder;
    bool enabled;
    bool isDefault;
} vm_net_mock_gateway_zone;

typedef struct
{
    u32 loaded;
    u32 skipped;
} vm_net_mock_gateway_zone_load_context;

static vm_net_mock_gateway_zone
    g_vm_net_mock_gateway_zones[VM_NET_MOCK_GATEWAY_ZONE_MAX];
static u32 g_vm_net_mock_gateway_zone_count = 0;
static bool g_vm_net_mock_gateway_zone_db_loaded = false;
static bool g_vm_net_mock_gateway_zone_db_valid = false;

static void vm_net_mock_gateway_zone_sort(void)
{
    for (u32 i = 1; i < g_vm_net_mock_gateway_zone_count; ++i)
    {
        vm_net_mock_gateway_zone value = g_vm_net_mock_gateway_zones[i];
        u32 j = i;

        while (j > 0)
        {
            const vm_net_mock_gateway_zone *previous =
                &g_vm_net_mock_gateway_zones[j - 1];
            if (previous->sortOrder < value.sortOrder ||
                (previous->sortOrder == value.sortOrder &&
                 previous->zoneId <= value.zoneId))
            {
                break;
            }
            g_vm_net_mock_gateway_zones[j] = *previous;
            --j;
        }
        g_vm_net_mock_gateway_zones[j] = value;
    }
}

static bool vm_net_mock_gateway_zone_url_ok(const char *url, bool allowEmpty,
                                            bool requireWs)
{
    size_t len = 0;
    size_t i = 0;

    if (url == NULL)
        return allowEmpty;
    len = strlen(url);
    if (len == 0)
        return allowEmpty;
    if (requireWs)
    {
        if (len >= sizeof(((vm_net_mock_gateway_zone *)0)->wsUrl))
            return false;
        if (strncmp(url, "ws://", 5) != 0 && strncmp(url, "wss://", 6) != 0)
            return false;
    }
    else
    {
        if (len >= sizeof(((vm_net_mock_gateway_zone *)0)->accountWebUrl))
            return false;
        if (strncmp(url, "http://", 7) != 0 &&
            strncmp(url, "https://", 8) != 0)
            return false;
    }
    for (i = 0; i < len; ++i)
    {
        unsigned char ch = (unsigned char)url[i];
        if (ch < 0x20u || ch > 0x7eu || ch == ' ')
            return false;
    }
    return true;
}

static bool vm_net_mock_gateway_zone_db_row(
    void *contextValue, unsigned int columnCount,
    const char *const *values, const size_t *lengths)
{
    vm_net_mock_gateway_zone_load_context *context =
        (vm_net_mock_gateway_zone_load_context *)contextValue;
    vm_net_mock_gateway_zone row;
    u32 number[4];
    size_t nameLen = 0;
    size_t labelLen = 0;
    size_t wsLen = 0;
    size_t webLen = 0;

    memset(&row, 0, sizeof(row));
    memset(number, 0, sizeof(number));
    if (context == NULL || columnCount != 8 ||
        g_vm_net_mock_gateway_zone_count >= VM_NET_MOCK_GATEWAY_ZONE_MAX ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &number[0]) ||
        number[0] == 0 ||
        !vm_mysql_hex_decode(values[1], lengths[1], row.displayName,
                             sizeof(row.displayName) - 1, &nameLen) ||
        !vm_mysql_hex_decode(values[2], lengths[2], row.label,
                             sizeof(row.label) - 1, &labelLen) ||
        !vm_mysql_hex_decode(values[3], lengths[3], row.wsUrl,
                             sizeof(row.wsUrl) - 1, &wsLen) ||
        !vm_mysql_hex_decode(values[4], lengths[4], row.accountWebUrl,
                             sizeof(row.accountWebUrl) - 1, &webLen) ||
        !vm_mock_mysql_parse_u32(values[5], lengths[5], &number[1]) ||
        !vm_mock_mysql_parse_u32(values[6], lengths[6], &number[2]) ||
        number[2] > 1 ||
        !vm_mock_mysql_parse_u32(values[7], lengths[7], &number[3]) ||
        number[3] > 1 || nameLen == 0 || wsLen == 0)
    {
        if (context != NULL)
            ++context->skipped;
        return true;
    }
    row.displayName[nameLen] = 0;
    row.label[labelLen] = 0;
    row.wsUrl[wsLen] = 0;
    row.accountWebUrl[webLen] = 0;
    if (!vm_net_mock_gateway_zone_url_ok(row.wsUrl, false, true) ||
        !vm_net_mock_gateway_zone_url_ok(row.accountWebUrl, true, false))
    {
        ++context->skipped;
        return true;
    }
    row.zoneId = number[0];
    row.sortOrder = number[1];
    row.enabled = number[2] != 0;
    row.isDefault = number[3] != 0;
    g_vm_net_mock_gateway_zones[g_vm_net_mock_gateway_zone_count++] = row;
    ++context->loaded;
    return true;
}

static u32 vm_net_mock_gateway_zone_enabled_count(void)
{
    u32 count = 0;

    for (u32 i = 0; i < g_vm_net_mock_gateway_zone_count; ++i)
    {
        if (g_vm_net_mock_gateway_zones[i].enabled)
            ++count;
    }
    return count;
}

static bool vm_net_mock_gateway_zone_db_load(void)
{
    vm_net_mock_gateway_zone_load_context context;

    if (g_vm_net_mock_gateway_zone_db_loaded)
        return g_vm_net_mock_gateway_zone_db_valid;
    g_vm_net_mock_gateway_zone_db_loaded = true;
    g_vm_net_mock_gateway_zone_db_valid = false;
    g_vm_net_mock_gateway_zone_count = 0;
    memset(g_vm_net_mock_gateway_zones, 0,
           sizeof(g_vm_net_mock_gateway_zones));
    memset(&context, 0, sizeof(context));

    /* UTF-8 seed: 江湖归来 / 推荐 / ws://192.1.1.1:19092 (matches Pixi bootstrap). */
    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_gateway_zones ("
            "zone_id INT UNSIGNED NOT NULL,"
            "display_name VARBINARY(63) NOT NULL,"
            "status_label VARBINARY(63) NOT NULL,"
            "ws_url VARBINARY(191) NOT NULL,"
            "account_web_url VARBINARY(191) NOT NULL,"
            "sort_order INT UNSIGNED NOT NULL DEFAULT 0,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "is_default TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(zone_id),"
            "KEY idx_server_gateway_zones_visible(enabled,sort_order,zone_id)"
            ") ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "INSERT IGNORE INTO server_gateway_zones("
            "zone_id,display_name,status_label,ws_url,account_web_url,"
            "sort_order,enabled,is_default) VALUES("
            "1,"
            "X'E6B19FE6B996E5BD92E69DA5',"
            "X'E68EA8E88D90',"
            "X'77733A2F2F3139322E312E312E313A3139303932',"
            "X'',"
            "0,1,1)") ||
        !vm_mysql_query(
            "SELECT zone_id,HEX(display_name),HEX(status_label),HEX(ws_url),"
            "HEX(account_web_url),sort_order,enabled,is_default "
            "FROM server_gateway_zones ORDER BY sort_order,zone_id",
            vm_net_mock_gateway_zone_db_row, &context))
    {
        printf("[error][network] gateway_zone_catalog_load failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    vm_net_mock_gateway_zone_sort();
    g_vm_net_mock_gateway_zone_db_valid = true;
    printf("[info][network] gateway_zone_catalog_load rows=%u enabled=%u skipped=%u source=mysql\n",
           g_vm_net_mock_gateway_zone_count,
           vm_net_mock_gateway_zone_enabled_count(), context.skipped);
    return true;
}

static void vm_net_mock_gateway_zone_clear_other_defaults(u32 keepZoneId)
{
    for (u32 i = 0; i < g_vm_net_mock_gateway_zone_count; ++i)
    {
        if (g_vm_net_mock_gateway_zones[i].zoneId != keepZoneId)
            g_vm_net_mock_gateway_zones[i].isDefault = false;
    }
}

static u32 vm_net_mock_gateway_zone_admin_list(
    vm_net_mock_gateway_zone *rows, u32 rowCap)
{
    u32 count = 0;

    if (rows == NULL || rowCap == 0 || !vm_net_mock_gateway_zone_db_load())
        return 0;
    count = g_vm_net_mock_gateway_zone_count;
    if (count > rowCap)
        count = rowCap;
    memcpy(rows, g_vm_net_mock_gateway_zones, count * sizeof(*rows));
    return count;
}

static u32 vm_net_mock_gateway_zone_public_list(
    vm_net_mock_gateway_zone *rows, u32 rowCap, u32 *defaultZoneIdOut)
{
    u32 count = 0;
    u32 defaultId = 0;

    if (defaultZoneIdOut)
        *defaultZoneIdOut = 0;
    if (!vm_net_mock_gateway_zone_db_load())
        return 0;
    for (u32 i = 0; i < g_vm_net_mock_gateway_zone_count; ++i)
    {
        const vm_net_mock_gateway_zone *row = &g_vm_net_mock_gateway_zones[i];
        if (!row->enabled)
            continue;
        if (row->isDefault)
            defaultId = row->zoneId;
        if (rows != NULL && count < rowCap)
            rows[count] = *row;
        ++count;
    }
    if (defaultId == 0 && count > 0)
    {
        for (u32 i = 0; i < g_vm_net_mock_gateway_zone_count; ++i)
        {
            if (g_vm_net_mock_gateway_zones[i].enabled)
            {
                defaultId = g_vm_net_mock_gateway_zones[i].zoneId;
                break;
            }
        }
    }
    if (defaultZoneIdOut)
        *defaultZoneIdOut = defaultId;
    if (rows == NULL)
        return count;
    return count > rowCap ? rowCap : count;
}

static bool vm_net_mock_gateway_zone_admin_save(
    const vm_net_mock_gateway_zone *row, const char **errorOut)
{
    char nameHex[VM_NET_MOCK_GATEWAY_ZONE_NAME_CAP * 2 + 1];
    char labelHex[VM_NET_MOCK_GATEWAY_ZONE_LABEL_CAP * 2 + 1];
    char wsHex[VM_NET_MOCK_GATEWAY_ZONE_WS_CAP * 2 + 1];
    char webHex[VM_NET_MOCK_GATEWAY_ZONE_WEB_CAP * 2 + 1];
    char query[1536];
    int existing = -1;

    if (errorOut)
        *errorOut = "区服网关参数无效";
    if (row == NULL || row->zoneId == 0 || row->displayName[0] == 0 ||
        strlen(row->displayName) >= sizeof(row->displayName) ||
        strlen(row->label) >= sizeof(row->label) ||
        !vm_net_mock_gateway_zone_url_ok(row->wsUrl, false, true) ||
        !vm_net_mock_gateway_zone_url_ok(row->accountWebUrl, true, false))
    {
        return false;
    }
    if (!vm_net_mock_gateway_zone_db_load())
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    for (u32 i = 0; i < g_vm_net_mock_gateway_zone_count; ++i)
    {
        if (g_vm_net_mock_gateway_zones[i].zoneId == row->zoneId)
        {
            existing = (int)i;
            break;
        }
    }
    if (existing < 0 &&
        g_vm_net_mock_gateway_zone_count >= VM_NET_MOCK_GATEWAY_ZONE_MAX)
    {
        if (errorOut)
            *errorOut = "区服网关最多支持 16 项";
        return false;
    }
    nameHex[0] = 0;
    labelHex[0] = 0;
    wsHex[0] = 0;
    webHex[0] = 0;
    /* hex_encode returns 0 both on failure and for empty input — only
     * treat 0 as failure when the source length is non-zero. */
    if ((row->displayName[0] != 0 &&
         vm_mysql_hex_encode(row->displayName, strlen(row->displayName),
                             nameHex, sizeof(nameHex)) == 0) ||
        (row->label[0] != 0 &&
         vm_mysql_hex_encode(row->label, strlen(row->label), labelHex,
                             sizeof(labelHex)) == 0) ||
        (row->wsUrl[0] != 0 &&
         vm_mysql_hex_encode(row->wsUrl, strlen(row->wsUrl), wsHex,
                             sizeof(wsHex)) == 0) ||
        (row->accountWebUrl[0] != 0 &&
         vm_mysql_hex_encode(row->accountWebUrl, strlen(row->accountWebUrl),
                             webHex, sizeof(webHex)) == 0))
    {
        if (errorOut)
            *errorOut = "区服网关文本编码失败";
        return false;
    }
    if (row->isDefault)
    {
        if (!vm_mysql_exec(
                "UPDATE server_gateway_zones SET is_default=0 WHERE is_default<>0"))
        {
            if (errorOut)
                *errorOut = vm_mysql_last_error();
            return false;
        }
        vm_net_mock_gateway_zone_clear_other_defaults(row->zoneId);
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_gateway_zones("
             "zone_id,display_name,status_label,ws_url,account_web_url,"
             "sort_order,enabled,is_default) "
             "VALUES(%u,X'%s',X'%s',X'%s',X'%s',%u,%u,%u) "
             "ON DUPLICATE KEY UPDATE "
             "display_name=VALUES(display_name),"
             "status_label=VALUES(status_label),"
             "ws_url=VALUES(ws_url),"
             "account_web_url=VALUES(account_web_url),"
             "sort_order=VALUES(sort_order),"
             "enabled=VALUES(enabled),"
             "is_default=VALUES(is_default)",
             row->zoneId, nameHex, labelHex, wsHex, webHex, row->sortOrder,
             row->enabled ? 1u : 0u, row->isDefault ? 1u : 0u);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (existing >= 0)
        g_vm_net_mock_gateway_zones[existing] = *row;
    else
        g_vm_net_mock_gateway_zones[g_vm_net_mock_gateway_zone_count++] = *row;
    vm_net_mock_gateway_zone_sort();
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] gateway_zone_save id=%u enabled=%u default=%u\n",
           row->zoneId, row->enabled ? 1u : 0u, row->isDefault ? 1u : 0u);
    return true;
}

static bool vm_net_mock_gateway_zone_admin_delete(u32 zoneId,
                                                  const char **errorOut)
{
    char query[128];
    int existing = -1;

    if (errorOut)
        *errorOut = "区服网关删除失败";
    if (zoneId == 0 || !vm_net_mock_gateway_zone_db_load())
    {
        if (errorOut)
            *errorOut = zoneId == 0 ? "区服 ID 无效" : vm_mysql_last_error();
        return false;
    }
    for (u32 i = 0; i < g_vm_net_mock_gateway_zone_count; ++i)
    {
        if (g_vm_net_mock_gateway_zones[i].zoneId == zoneId)
        {
            existing = (int)i;
            break;
        }
    }
    if (existing < 0)
    {
        if (errorOut)
            *errorOut = "区服网关不存在";
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM server_gateway_zones WHERE zone_id=%u", zoneId);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if ((u32)existing + 1 < g_vm_net_mock_gateway_zone_count)
    {
        memmove(&g_vm_net_mock_gateway_zones[existing],
                &g_vm_net_mock_gateway_zones[existing + 1],
                (g_vm_net_mock_gateway_zone_count - (u32)existing - 1) *
                    sizeof(g_vm_net_mock_gateway_zones[0]));
    }
    --g_vm_net_mock_gateway_zone_count;
    memset(&g_vm_net_mock_gateway_zones[g_vm_net_mock_gateway_zone_count], 0,
           sizeof(g_vm_net_mock_gateway_zones[0]));
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] gateway_zone_delete id=%u\n", zoneId);
    return true;
}
