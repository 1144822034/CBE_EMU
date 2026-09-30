/* Admin-created item / equip / skill catalog rows (MySQL extra).
 * Included from mock_server_catalog.c after DSH loaders exist.
 * IDs are assigned as max(existing space)+1 — never hand-picked. */

enum
{
    VM_NET_MOCK_CATALOG_EXTRA_MAX = 256,
    VM_NET_MOCK_CATALOG_EXTRA_NAME_BYTES = 31,
    VM_NET_MOCK_CATALOG_EXTRA_DESC_BYTES = 191
};

typedef struct
{
    u32 itemId;
    char name[VM_NET_MOCK_CATALOG_EXTRA_NAME_BYTES + 1];
    char description[VM_NET_MOCK_CATALOG_EXTRA_DESC_BYTES + 1];
    u8 category;
    u8 levelRequired;
    u8 stack;
    u8 consumeMode;
    u8 durationMinutes;
    u32 hp;
    u32 mp;
    u32 exp;
    u32 questLink;
    u32 bindAttr;
    u8 visual;
    u32 price;
    u32 coolPrice;
    u8 shopEnabled;
} vm_net_mock_item_catalog_extra_row;

typedef struct
{
    u32 itemId;
    char name[VM_NET_MOCK_CATALOG_EXTRA_NAME_BYTES + 1];
    char description[VM_NET_MOCK_CATALOG_EXTRA_DESC_BYTES + 1];
    u8 category;
    u8 levelRequired;
    u8 quality;
    u16 durabilityMax;
    vm_net_mock_equipment_bonus bonus;
    u32 price;
    u8 shopEnabled;
} vm_net_mock_equip_catalog_extra_row;

typedef struct
{
    u32 skillId;
    char name[VM_NET_MOCK_CATALOG_EXTRA_NAME_BYTES + 1];
    char description[VM_NET_MOCK_CATALOG_EXTRA_DESC_BYTES + 1];
    u32 effectIndex;
    u8 levelRequired;
    u8 rawJob;
    u32 learnPrice;
    u8 useLimit;
    u8 targetDirection;
    u32 mpCost;
    int32_t hpChange;
    u32 strengthCoeff;
    u32 agilityCoeff;
    u32 wisdomCoeff;
    u8 durationRounds;
    int32_t strengthChange;
    int32_t agilityChange;
    int32_t wisdomChange;
    int32_t attackChange;
    int32_t defenseChange;
    int32_t critChange;
    int32_t hitChange;
    int32_t dodgeChange;
    int32_t resistChange;
    u8 effectKind;
} vm_net_mock_skill_catalog_extra_row;

static vm_net_mock_item_catalog_extra_row
    g_vm_net_mock_item_catalog_extra[VM_NET_MOCK_CATALOG_EXTRA_MAX];
static u32 g_vm_net_mock_item_catalog_extra_count = 0;
static vm_net_mock_equip_catalog_extra_row
    g_vm_net_mock_equip_catalog_extra[VM_NET_MOCK_CATALOG_EXTRA_MAX];
static u32 g_vm_net_mock_equip_catalog_extra_count = 0;
static vm_net_mock_skill_catalog_extra_row
    g_vm_net_mock_skill_catalog_extra[VM_NET_MOCK_CATALOG_EXTRA_MAX];
static u32 g_vm_net_mock_skill_catalog_extra_count = 0;
static bool g_vm_net_mock_catalog_extra_db_loaded = false;
static bool g_vm_net_mock_catalog_extra_db_valid = false;
static bool g_vm_net_mock_catalog_extra_shop_applied = false;
static bool g_vm_net_mock_catalog_extra_effect_applied = false;
static bool g_vm_net_mock_catalog_extra_equip_applied = false;
static bool g_vm_net_mock_catalog_extra_skill_applied = false;
/* Bumped on create/save/delete so Pixi can refresh without process restart. */
static u32 g_vm_net_mock_catalog_extra_rev = 1;

static void vm_net_mock_catalog_extra_bump_rev(void)
{
    if (g_vm_net_mock_catalog_extra_rev == 0xffffffffu)
        g_vm_net_mock_catalog_extra_rev = 1u;
    else
        ++g_vm_net_mock_catalog_extra_rev;
}

typedef struct
{
    u32 value;
    bool found;
    bool invalid;
} vm_net_mock_catalog_extra_count_ctx;

static bool vm_net_mock_catalog_extra_parse_u32(const char *text, u32 *valueOut)
{
    char *end = NULL;
    unsigned long parsed = 0;

    if (text == NULL || text[0] == 0 || valueOut == NULL)
        return false;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != 0 || parsed > 0xfffffffful)
        return false;
    *valueOut = (u32)parsed;
    return true;
}

/*
 * MySQL text protocol fields are length-prefixed and NOT NUL-terminated in the
 * packet buffer. Parsing with strtoul/sscanf on raw pointers reads into the next
 * column (e.g. item_id "50021" + HEX(name) "D0C2...") and fails `*end == 0`.
 */
static bool vm_net_mock_catalog_extra_parse_u32_n(const char *text, size_t len,
                                                 u32 *valueOut)
{
    char buf[32];

    if (text == NULL || valueOut == NULL || len == 0 || len >= sizeof(buf))
        return false;
    memcpy(buf, text, len);
    buf[len] = 0;
    return vm_net_mock_catalog_extra_parse_u32(buf, valueOut);
}

static bool vm_net_mock_catalog_extra_parse_i32_n(const char *text, size_t len,
                                                 int32_t *valueOut)
{
    char buf[32];
    char *end = NULL;
    long parsed = 0;

    if (text == NULL || valueOut == NULL || len == 0 || len >= sizeof(buf))
        return false;
    memcpy(buf, text, len);
    buf[len] = 0;
    errno = 0;
    parsed = strtol(buf, &end, 10);
    if (errno != 0 || end == buf || *end != 0)
        return false;
    if (parsed < (-2147483647L - 1L) || parsed > 2147483647L)
        return false;
    *valueOut = (int32_t)parsed;
    return true;
}

static bool vm_net_mock_catalog_extra_count_row(void *contextValue,
                                               unsigned int columnCount,
                                               const char *const *values,
                                               const size_t *lengths)
{
    vm_net_mock_catalog_extra_count_ctx *ctx =
        (vm_net_mock_catalog_extra_count_ctx *)contextValue;
    u32 parsed = 0;

    if (ctx == NULL || columnCount < 1 || values == NULL || values[0] == NULL ||
        lengths == NULL)
    {
        if (ctx)
            ctx->invalid = true;
        return true;
    }
    if (!vm_net_mock_catalog_extra_parse_u32_n(values[0], lengths[0], &parsed))
    {
        ctx->invalid = true;
        return true;
    }
    ctx->value = parsed;
    ctx->found = true;
    return true;
}

static bool vm_net_mock_catalog_extra_sql_count(const char *sql, u32 *out)
{
    vm_net_mock_catalog_extra_count_ctx ctx;

    if (out)
        *out = 0;
    memset(&ctx, 0, sizeof(ctx));
    if (!vm_mysql_query(sql, vm_net_mock_catalog_extra_count_row, &ctx) ||
        ctx.invalid || !ctx.found)
        return false;
    if (out)
        *out = ctx.value;
    return true;
}

static int vm_net_mock_item_catalog_extra_index(u32 itemId)
{
    for (u32 i = 0; i < g_vm_net_mock_item_catalog_extra_count; ++i)
    {
        if (g_vm_net_mock_item_catalog_extra[i].itemId == itemId)
            return (int)i;
    }
    return -1;
}

static int vm_net_mock_equip_catalog_extra_index(u32 itemId)
{
    for (u32 i = 0; i < g_vm_net_mock_equip_catalog_extra_count; ++i)
    {
        if (g_vm_net_mock_equip_catalog_extra[i].itemId == itemId)
            return (int)i;
    }
    return -1;
}

static int vm_net_mock_skill_catalog_extra_index(u32 skillId)
{
    for (u32 i = 0; i < g_vm_net_mock_skill_catalog_extra_count; ++i)
    {
        if (g_vm_net_mock_skill_catalog_extra[i].skillId == skillId)
            return (int)i;
    }
    return -1;
}

static vm_net_mock_shop_catalog_item *vm_net_mock_find_shop_catalog_item_mut(
    u32 itemId)
{
    u32 total = vm_net_mock_load_shop_catalog();

    for (u32 i = 0; i < total; ++i)
    {
        if (g_vm_net_mock_shop_catalog[i].itemId == itemId)
            return &g_vm_net_mock_shop_catalog[i];
    }
    return NULL;
}

static vm_net_mock_item_effect_catalog_item *
vm_net_mock_find_item_effect_catalog_item_mut(u32 itemId)
{
    u32 total = vm_net_mock_load_item_effect_catalog();

    for (u32 i = 0; i < total; ++i)
    {
        if (g_vm_net_mock_item_effect_catalog[i].itemId == itemId)
            return &g_vm_net_mock_item_effect_catalog[i];
    }
    return NULL;
}

static vm_net_mock_equipment_catalog_item *
vm_net_mock_find_equipment_catalog_item_mut(u32 itemId)
{
    u32 total = vm_net_mock_load_equipment_catalog();

    for (u32 i = 0; i < total; ++i)
    {
        if (g_vm_net_mock_equipment_catalog[i].itemId == itemId)
            return &g_vm_net_mock_equipment_catalog[i];
    }
    return NULL;
}

static vm_net_mock_skill_catalog_item *vm_net_mock_find_skill_catalog_item_mut(
    u32 skillId)
{
    u32 total = vm_net_mock_load_skill_catalog();

    for (u32 i = 0; i < total; ++i)
    {
        if (g_vm_net_mock_skill_catalog[i].skillId == skillId)
            return &g_vm_net_mock_skill_catalog[i];
    }
    return NULL;
}

static void vm_net_mock_catalog_extra_copy_text(char *dst, size_t dstCap,
                                               const char *src, size_t srcLen,
                                               size_t maxBytes)
{
    size_t n = srcLen;

    if (dst == NULL || dstCap == 0)
        return;
    dst[0] = 0;
    if (src == NULL || n == 0)
        return;
    if (n > maxBytes)
        n = maxBytes;
    if (n >= dstCap)
        n = dstCap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static bool vm_net_mock_catalog_extra_item_row(void *contextValue,
                                              unsigned int columnCount,
                                              const char *const *values,
                                              const size_t *lengths)
{
    vm_net_mock_item_catalog_extra_row *row = NULL;
    char nameBuf[64];
    char descBuf[256];
    u32 itemId = 0;
    u32 tmp = 0;
    size_t decoded = 0;

    (void)contextValue;
    if (columnCount < 17 || values == NULL || lengths == NULL)
        return true;
    if (g_vm_net_mock_item_catalog_extra_count >= VM_NET_MOCK_CATALOG_EXTRA_MAX)
        return true;
    if (!vm_net_mock_catalog_extra_parse_u32_n(values[0], lengths[0], &itemId) || itemId == 0)
        return true;
    memset(nameBuf, 0, sizeof(nameBuf));
    memset(descBuf, 0, sizeof(descBuf));
    if (!vm_mysql_hex_decode(values[1], lengths[1], nameBuf, sizeof(nameBuf),
                             &decoded) ||
        nameBuf[0] == 0)
        return true;
    (void)vm_mysql_hex_decode(values[2], lengths[2], descBuf, sizeof(descBuf),
                              &decoded);
    row = &g_vm_net_mock_item_catalog_extra[g_vm_net_mock_item_catalog_extra_count++];
    memset(row, 0, sizeof(*row));
    row->itemId = itemId;
    vm_net_mock_catalog_extra_copy_text(row->name, sizeof(row->name), nameBuf,
                                        strlen(nameBuf),
                                        VM_NET_MOCK_CATALOG_EXTRA_NAME_BYTES);
    vm_net_mock_catalog_extra_copy_text(row->description, sizeof(row->description),
                                        descBuf, strlen(descBuf),
                                        VM_NET_MOCK_CATALOG_EXTRA_DESC_BYTES);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[3], lengths[3], &tmp))
        row->category = (u8)(tmp > 255 ? 255 : tmp);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[4], lengths[4], &tmp))
        row->levelRequired = (u8)(tmp ? (tmp > 255 ? 255 : tmp) : 1);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[5], lengths[5], &tmp))
        row->stack = (u8)(tmp ? (tmp > 255 ? 255 : tmp) : 1);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[6], lengths[6], &tmp))
        row->consumeMode = (u8)(tmp > 255 ? 255 : tmp);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[7], lengths[7], &tmp))
        row->durationMinutes = (u8)(tmp > 255 ? 255 : tmp);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[8], lengths[8], &row->hp);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[9], lengths[9], &row->mp);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[10], lengths[10], &row->exp);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[11], lengths[11], &row->questLink);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[12], lengths[12], &row->bindAttr);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[13], lengths[13], &tmp))
        row->visual = (u8)(tmp ? (tmp > 255 ? 255 : tmp) : 1);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[14], lengths[14], &row->price);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[15], lengths[15], &row->coolPrice);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[16], lengths[16], &tmp))
        row->shopEnabled = tmp ? 1u : 0u;
    return true;
}

static bool vm_net_mock_catalog_extra_equip_row(void *contextValue,
                                               unsigned int columnCount,
                                               const char *const *values,
                                               const size_t *lengths)
{
    vm_net_mock_equip_catalog_extra_row *row = NULL;
    char nameBuf[64];
    char descBuf[256];
    u32 itemId = 0;
    u32 tmp = 0;
    size_t decoded = 0;

    (void)contextValue;
    if (columnCount < 20 || values == NULL || lengths == NULL)
        return true;
    if (g_vm_net_mock_equip_catalog_extra_count >= VM_NET_MOCK_CATALOG_EXTRA_MAX)
        return true;
    if (!vm_net_mock_catalog_extra_parse_u32_n(values[0], lengths[0], &itemId) || itemId == 0)
        return true;
    memset(nameBuf, 0, sizeof(nameBuf));
    memset(descBuf, 0, sizeof(descBuf));
    if (!vm_mysql_hex_decode(values[1], lengths[1], nameBuf, sizeof(nameBuf),
                             &decoded) ||
        nameBuf[0] == 0)
        return true;
    (void)vm_mysql_hex_decode(values[2], lengths[2], descBuf, sizeof(descBuf),
                              &decoded);
    row = &g_vm_net_mock_equip_catalog_extra[g_vm_net_mock_equip_catalog_extra_count++];
    memset(row, 0, sizeof(*row));
    row->itemId = itemId;
    vm_net_mock_catalog_extra_copy_text(row->name, sizeof(row->name), nameBuf,
                                        strlen(nameBuf),
                                        VM_NET_MOCK_CATALOG_EXTRA_NAME_BYTES);
    vm_net_mock_catalog_extra_copy_text(row->description, sizeof(row->description),
                                        descBuf, strlen(descBuf),
                                        VM_NET_MOCK_CATALOG_EXTRA_DESC_BYTES);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[3], lengths[3], &tmp))
        row->category = (u8)(tmp > 255 ? 255 : tmp);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[4], lengths[4], &tmp))
        row->levelRequired = (u8)(tmp ? (tmp > 255 ? 255 : tmp) : 1);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[5], lengths[5], &tmp))
        row->quality = (u8)(tmp > 255 ? 255 : tmp);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[6], lengths[6], &tmp))
        row->durabilityMax = (u16)(tmp ? (tmp > 0xffffu ? 0xffffu : tmp) : 50);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[7], lengths[7], &row->bonus.armor);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[8], lengths[8], &row->bonus.attack);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[9], lengths[9], &row->bonus.hp);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[10], lengths[10], &row->bonus.mp);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[11], lengths[11], &row->bonus.strength);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[12], lengths[12], &row->bonus.agility);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[13], lengths[13], &row->bonus.wisdom);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[14], lengths[14], &row->bonus.dodge);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[15], lengths[15], &row->bonus.hit);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[16], lengths[16], &row->bonus.crit);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[17], lengths[17], &row->bonus.resist);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[18], lengths[18], &row->price);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[19], lengths[19], &tmp))
        row->shopEnabled = tmp ? 1u : 0u;
    return true;
}

static bool vm_net_mock_catalog_extra_skill_row(void *contextValue,
                                               unsigned int columnCount,
                                               const char *const *values,
                                               const size_t *lengths)
{
    vm_net_mock_skill_catalog_extra_row *row = NULL;
    char nameBuf[64];
    char descBuf[256];
    u32 skillId = 0;
    u32 tmp = 0;
    int32_t signedTmp = 0;
    size_t decoded = 0;

    (void)contextValue;
    if (columnCount < 26 || values == NULL || lengths == NULL)
        return true;
    if (g_vm_net_mock_skill_catalog_extra_count >= VM_NET_MOCK_CATALOG_EXTRA_MAX)
        return true;
    if (!vm_net_mock_catalog_extra_parse_u32_n(values[0], lengths[0], &skillId) || skillId == 0)
        return true;
    memset(nameBuf, 0, sizeof(nameBuf));
    memset(descBuf, 0, sizeof(descBuf));
    if (!vm_mysql_hex_decode(values[1], lengths[1], nameBuf, sizeof(nameBuf),
                             &decoded) ||
        nameBuf[0] == 0)
        return true;
    (void)vm_mysql_hex_decode(values[2], lengths[2], descBuf, sizeof(descBuf),
                              &decoded);
    row = &g_vm_net_mock_skill_catalog_extra[g_vm_net_mock_skill_catalog_extra_count++];
    memset(row, 0, sizeof(*row));
    row->skillId = skillId;
    vm_net_mock_catalog_extra_copy_text(row->name, sizeof(row->name), nameBuf,
                                        strlen(nameBuf),
                                        VM_NET_MOCK_CATALOG_EXTRA_NAME_BYTES);
    vm_net_mock_catalog_extra_copy_text(row->description, sizeof(row->description),
                                        descBuf, strlen(descBuf),
                                        VM_NET_MOCK_CATALOG_EXTRA_DESC_BYTES);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[3], lengths[3], &row->effectIndex);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[4], lengths[4], &tmp))
        row->levelRequired = (u8)(tmp ? (tmp > 255 ? 255 : tmp) : 1);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[5], lengths[5], &tmp))
        row->rawJob = (u8)(tmp > 2 ? 2 : tmp);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[6], lengths[6], &row->learnPrice);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[7], lengths[7], &tmp))
        row->useLimit = (u8)(tmp > 255 ? 255 : tmp);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[8], lengths[8], &tmp))
        row->targetDirection = (u8)(tmp > 255 ? 255 : tmp);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[9], lengths[9], &row->mpCost);
    if (vm_net_mock_catalog_extra_parse_i32_n(values[10], lengths[10], &signedTmp))
        row->hpChange = signedTmp;
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[11], lengths[11], &row->strengthCoeff);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[12], lengths[12], &row->agilityCoeff);
    (void)vm_net_mock_catalog_extra_parse_u32_n(values[13], lengths[13], &row->wisdomCoeff);
    if (vm_net_mock_catalog_extra_parse_u32_n(values[14], lengths[14], &tmp))
        row->durationRounds = (u8)(tmp > 255 ? 255 : tmp);
#define VM_CATALOG_EXTRA_PARSE_I32(idx, field)                                 \
    do                                                                         \
    {                                                                          \
        if (vm_net_mock_catalog_extra_parse_i32_n(values[(idx)], lengths[(idx)], \
                                                 &signedTmp))                  \
            row->field = signedTmp;                                            \
    } while (0)
    VM_CATALOG_EXTRA_PARSE_I32(15, strengthChange);
    VM_CATALOG_EXTRA_PARSE_I32(16, agilityChange);
    VM_CATALOG_EXTRA_PARSE_I32(17, wisdomChange);
    VM_CATALOG_EXTRA_PARSE_I32(18, attackChange);
    VM_CATALOG_EXTRA_PARSE_I32(19, defenseChange);
    VM_CATALOG_EXTRA_PARSE_I32(20, critChange);
    VM_CATALOG_EXTRA_PARSE_I32(21, hitChange);
    VM_CATALOG_EXTRA_PARSE_I32(22, dodgeChange);
    VM_CATALOG_EXTRA_PARSE_I32(23, resistChange);
#undef VM_CATALOG_EXTRA_PARSE_I32
    if (vm_net_mock_catalog_extra_parse_u32_n(values[24], lengths[24], &tmp))
        row->effectKind = (u8)(tmp > 255 ? 255 : tmp);
    return true;
}

static bool vm_net_mock_catalog_extra_db_load_unlocked(void)
{
    if (g_vm_net_mock_catalog_extra_db_loaded)
        return g_vm_net_mock_catalog_extra_db_valid;

    g_vm_net_mock_catalog_extra_db_loaded = true;
    g_vm_net_mock_catalog_extra_db_valid = false;
    g_vm_net_mock_item_catalog_extra_count = 0;
    g_vm_net_mock_equip_catalog_extra_count = 0;
    g_vm_net_mock_skill_catalog_extra_count = 0;
    g_vm_net_mock_catalog_extra_shop_applied = false;
    g_vm_net_mock_catalog_extra_effect_applied = false;
    g_vm_net_mock_catalog_extra_equip_applied = false;
    g_vm_net_mock_catalog_extra_skill_applied = false;

    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_item_catalog_extra ("
            "item_id INT UNSIGNED NOT NULL,"
            "name VARBINARY(31) NOT NULL,"
            "description VARBINARY(191) NOT NULL DEFAULT '',"
            "category TINYINT UNSIGNED NOT NULL DEFAULT 10,"
            "level_required TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "stack TINYINT UNSIGNED NOT NULL DEFAULT 99,"
            "consume_mode TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "duration_minutes TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "hp INT UNSIGNED NOT NULL DEFAULT 0,"
            "mp INT UNSIGNED NOT NULL DEFAULT 0,"
            "exp INT UNSIGNED NOT NULL DEFAULT 0,"
            "quest_link INT UNSIGNED NOT NULL DEFAULT 0,"
            "bind_attr INT UNSIGNED NOT NULL DEFAULT 0,"
            "visual TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "price INT UNSIGNED NOT NULL DEFAULT 0,"
            "cool_price INT UNSIGNED NOT NULL DEFAULT 0,"
            "shop_enabled TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(item_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_equip_catalog_extra ("
            "item_id INT UNSIGNED NOT NULL,"
            "name VARBINARY(31) NOT NULL,"
            "description VARBINARY(191) NOT NULL DEFAULT '',"
            "category TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "level_required TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "quality TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "durability_max SMALLINT UNSIGNED NOT NULL DEFAULT 50,"
            "armor INT UNSIGNED NOT NULL DEFAULT 0,"
            "attack INT UNSIGNED NOT NULL DEFAULT 0,"
            "hp INT UNSIGNED NOT NULL DEFAULT 0,"
            "mp INT UNSIGNED NOT NULL DEFAULT 0,"
            "strength INT UNSIGNED NOT NULL DEFAULT 0,"
            "agility INT UNSIGNED NOT NULL DEFAULT 0,"
            "wisdom INT UNSIGNED NOT NULL DEFAULT 0,"
            "dodge INT UNSIGNED NOT NULL DEFAULT 0,"
            "hit INT UNSIGNED NOT NULL DEFAULT 0,"
            "crit INT UNSIGNED NOT NULL DEFAULT 0,"
            "resist INT UNSIGNED NOT NULL DEFAULT 0,"
            "price INT UNSIGNED NOT NULL DEFAULT 0,"
            "shop_enabled TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(item_id)) ENGINE=InnoDB") ||
        !vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_skill_catalog_extra ("
            "skill_id INT UNSIGNED NOT NULL,"
            "name VARBINARY(31) NOT NULL,"
            "description VARBINARY(191) NOT NULL DEFAULT '',"
            "effect_index INT UNSIGNED NOT NULL DEFAULT 0,"
            "level_required TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "raw_job TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "learn_price INT UNSIGNED NOT NULL DEFAULT 0,"
            "use_limit TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "target_direction TINYINT UNSIGNED NOT NULL DEFAULT 3,"
            "mp_cost INT UNSIGNED NOT NULL DEFAULT 0,"
            "hp_change INT NOT NULL DEFAULT 0,"
            "strength_coeff INT UNSIGNED NOT NULL DEFAULT 0,"
            "agility_coeff INT UNSIGNED NOT NULL DEFAULT 0,"
            "wisdom_coeff INT UNSIGNED NOT NULL DEFAULT 0,"
            "duration_rounds TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "strength_change INT NOT NULL DEFAULT 0,"
            "agility_change INT NOT NULL DEFAULT 0,"
            "wisdom_change INT NOT NULL DEFAULT 0,"
            "attack_change INT NOT NULL DEFAULT 0,"
            "defense_change INT NOT NULL DEFAULT 0,"
            "crit_change INT NOT NULL DEFAULT 0,"
            "hit_change INT NOT NULL DEFAULT 0,"
            "dodge_change INT NOT NULL DEFAULT 0,"
            "resist_change INT NOT NULL DEFAULT 0,"
            "effect_kind TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(skill_id)) ENGINE=InnoDB") ||
        !vm_mysql_query(
            "SELECT item_id,HEX(name),HEX(description),category,level_required,"
            "stack,consume_mode,duration_minutes,hp,mp,exp,quest_link,bind_attr,"
            "visual,price,cool_price,shop_enabled "
            "FROM server_item_catalog_extra ORDER BY item_id",
            vm_net_mock_catalog_extra_item_row, NULL) ||
        !vm_mysql_query(
            "SELECT item_id,HEX(name),HEX(description),category,level_required,"
            "quality,durability_max,armor,attack,hp,mp,strength,agility,wisdom,"
            "dodge,hit,crit,resist,price,shop_enabled "
            "FROM server_equip_catalog_extra ORDER BY item_id",
            vm_net_mock_catalog_extra_equip_row, NULL) ||
        !vm_mysql_query(
            "SELECT skill_id,HEX(name),HEX(description),effect_index,"
            "level_required,raw_job,learn_price,use_limit,target_direction,"
            "mp_cost,hp_change,strength_coeff,agility_coeff,wisdom_coeff,"
            "duration_rounds,strength_change,agility_change,wisdom_change,"
            "attack_change,defense_change,crit_change,hit_change,dodge_change,"
            "resist_change,effect_kind "
            "FROM server_skill_catalog_extra ORDER BY skill_id",
            vm_net_mock_catalog_extra_skill_row, NULL))
    {
        printf("[error][mock-admin] catalog_extra_db_load failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }

    g_vm_net_mock_catalog_extra_db_valid = true;
    printf("[info][mock-admin] catalog_extra_db_load items=%u equips=%u "
           "skills=%u\n",
           g_vm_net_mock_item_catalog_extra_count,
           g_vm_net_mock_equip_catalog_extra_count,
           g_vm_net_mock_skill_catalog_extra_count);
    return true;
}

static bool vm_net_mock_catalog_extra_db_load(void)
{
    static int s_loading = 0;
    bool ok;

    if (g_vm_net_mock_catalog_extra_db_loaded)
        return g_vm_net_mock_catalog_extra_db_valid;
    if (s_loading)
        return false;
    s_loading = 1;
    ok = vm_net_mock_catalog_extra_db_load_unlocked();
    s_loading = 0;
    return ok;
}

/* Re-read MySQL extras into memory (Pixi API / admin after sticky empty load). */
static bool vm_net_mock_catalog_extra_db_force_reload(void)
{
    static int s_reloading = 0;
    bool ok;

    if (s_reloading)
        return g_vm_net_mock_catalog_extra_db_valid;
    s_reloading = 1;
    g_vm_net_mock_catalog_extra_db_loaded = false;
    g_vm_net_mock_catalog_extra_db_valid = false;
    ok = vm_net_mock_catalog_extra_db_load();
    /*
     * load_unlocked clears *applied flags. Re-run injectors — each apply_*
     * skips ids already present, so existing shop/equip/skill rows stay put
     * while previously-skipped MySQL rows (NUL-parse bug) get injected.
     */
    if (ok)
    {
        g_vm_net_mock_catalog_extra_shop_applied = false;
        g_vm_net_mock_catalog_extra_effect_applied = false;
        g_vm_net_mock_catalog_extra_equip_applied = false;
        g_vm_net_mock_catalog_extra_skill_applied = false;
        vm_net_mock_catalog_extra_apply_to_shop_catalog();
        vm_net_mock_catalog_extra_apply_to_item_effect_catalog();
        vm_net_mock_catalog_extra_apply_to_equipment_catalog();
        vm_net_mock_catalog_extra_apply_to_skill_catalog();
    }
    s_reloading = 0;
    return ok;
}

static u32 vm_net_mock_catalog_extra_shop_price(
    const vm_net_mock_item_catalog_extra_row *row)
{
    if (row == NULL)
        return VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE;
    if (row->coolPrice != 0)
        return row->coolPrice;
    if (row->price != 0)
        return row->price;
    return VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE;
}

/* 自定义道具铜币价：优先「铜价格」字段，否则与商城价同源. */
static u32 vm_net_mock_catalog_extra_npc_price(
    const vm_net_mock_item_catalog_extra_row *row, u32 mallPrice)
{
    if (row != NULL && row->price != 0)
        return row->price;
    if (mallPrice != 0)
        return mallPrice;
    return VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE;
}

static void vm_net_mock_catalog_extra_apply_item_to_shop(
    const vm_net_mock_item_catalog_extra_row *row)
{
    vm_net_mock_shop_catalog_item *shop = NULL;
    u32 price = 0;
    u32 npcPrice = 0;

    if (row == NULL || row->itemId == 0)
        return;
    price = vm_net_mock_catalog_extra_shop_price(row);
    npcPrice = vm_net_mock_catalog_extra_npc_price(row, price);
    shop = vm_net_mock_find_shop_catalog_item_mut(row->itemId);
    if (shop != NULL)
    {
        printf("[warn][mock-admin] catalog_extra item shop skip id=%u "
               "(already present)\n",
               row->itemId);
        return;
    }
    if (!vm_net_mock_add_shop_catalog_item(
            row->itemId, (const u8 *)row->name, (u32)strlen(row->name),
            (const u8 *)row->description, (u32)strlen(row->description), price,
            row->stack ? row->stack : 99, row->stack ? row->stack : 99,
            row->visual ? row->visual : 1, false, row->category))
    {
        printf("[warn][mock-admin] catalog_extra item shop add failed id=%u\n",
               row->itemId);
        return;
    }
    shop = vm_net_mock_find_shop_catalog_item_mut(row->itemId);
    if (shop != NULL)
    {
        shop->enabled = row->shopEnabled ? 1u : 0u;
        shop->npcPrice = npcPrice;
        if (row->shopEnabled)
        {
            const char *err = NULL;
            (void)vm_net_mock_shop_admin_save(row->itemId, price, npcPrice, true,
                                              shop->npcEnabled != 0, &err);
        }
    }
}

static void vm_net_mock_catalog_extra_apply_item_to_effect(
    const vm_net_mock_item_catalog_extra_row *row)
{
    if (row == NULL || row->itemId == 0)
        return;
    if (vm_net_mock_find_item_effect_catalog_item_mut(row->itemId) != NULL)
    {
        printf("[warn][mock-admin] catalog_extra item effect skip id=%u\n",
               row->itemId);
        return;
    }
    if (!vm_net_mock_add_item_effect_catalog_item(
            row->itemId, row->category, row->levelRequired,
            row->stack ? row->stack : 1, row->consumeMode, row->durationMinutes,
            row->hp, row->mp, row->exp, row->questLink, row->bindAttr))
    {
        printf("[warn][mock-admin] catalog_extra item effect add failed id=%u\n",
               row->itemId);
    }
}

static void vm_net_mock_catalog_extra_apply_equip_to_shop(
    const vm_net_mock_equip_catalog_extra_row *row)
{
    vm_net_mock_shop_catalog_item *shop = NULL;
    u32 price = 0;

    if (row == NULL || row->itemId == 0)
        return;
    price = row->price ? row->price : VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE;
    shop = vm_net_mock_find_shop_catalog_item_mut(row->itemId);
    if (shop != NULL)
    {
        printf("[warn][mock-admin] catalog_extra equip shop skip id=%u\n",
               row->itemId);
        return;
    }
    if (!vm_net_mock_add_shop_catalog_item(
            row->itemId, (const u8 *)row->name, (u32)strlen(row->name),
            (const u8 *)row->description, (u32)strlen(row->description), price,
            1, 1, 1, true, row->category))
    {
        printf("[warn][mock-admin] catalog_extra equip shop add failed id=%u\n",
               row->itemId);
        return;
    }
    shop = vm_net_mock_find_shop_catalog_item_mut(row->itemId);
    if (shop != NULL)
    {
        shop->enabled = row->shopEnabled ? 1u : 0u;
        shop->npcPrice = price;
        if (row->shopEnabled)
        {
            const char *err = NULL;
            (void)vm_net_mock_shop_admin_save(row->itemId, price, price, true,
                                              shop->npcEnabled != 0, &err);
        }
    }
}

static void vm_net_mock_catalog_extra_apply_equip_to_stats(
    const vm_net_mock_equip_catalog_extra_row *row)
{
    if (row == NULL || row->itemId == 0)
        return;
    if (vm_net_mock_find_equipment_catalog_item_mut(row->itemId) != NULL)
    {
        printf("[warn][mock-admin] catalog_extra equip stats skip id=%u\n",
               row->itemId);
        return;
    }
    if (!vm_net_mock_add_equipment_catalog_item(
            row->itemId, row->levelRequired, row->quality, row->category,
            row->durabilityMax ? row->durabilityMax : 50, &row->bonus))
    {
        printf("[warn][mock-admin] catalog_extra equip stats add failed id=%u\n",
               row->itemId);
    }
}

static void vm_net_mock_catalog_extra_apply_skill(
    const vm_net_mock_skill_catalog_extra_row *row)
{
    if (row == NULL || row->skillId == 0)
        return;
    if (vm_net_mock_find_skill_catalog_item_mut(row->skillId) != NULL)
    {
        printf("[warn][mock-admin] catalog_extra skill skip id=%u\n",
               row->skillId);
        return;
    }
    if (!vm_net_mock_add_skill_catalog_item(
            row->skillId, row->rawJob, row->levelRequired, row->effectIndex,
            row->learnPrice, row->mpCost, row->hpChange, row->strengthCoeff,
            row->agilityCoeff, row->wisdomCoeff, row->targetDirection,
            row->useLimit, row->durationRounds, row->strengthChange,
            row->agilityChange, row->wisdomChange, row->attackChange,
            row->defenseChange, row->critChange, row->hitChange,
            row->dodgeChange, row->resistChange, row->effectKind,
            (const u8 *)row->name, (u32)strlen(row->name)))
    {
        printf("[warn][mock-admin] catalog_extra skill add failed id=%u\n",
               row->skillId);
    }
}

static void vm_net_mock_catalog_extra_apply_to_shop_catalog(void)
{
    if (!vm_net_mock_catalog_extra_db_load() ||
        g_vm_net_mock_catalog_extra_shop_applied)
        return;
    g_vm_net_mock_catalog_extra_shop_applied = true;
    for (u32 i = 0; i < g_vm_net_mock_item_catalog_extra_count; ++i)
        vm_net_mock_catalog_extra_apply_item_to_shop(
            &g_vm_net_mock_item_catalog_extra[i]);
    for (u32 i = 0; i < g_vm_net_mock_equip_catalog_extra_count; ++i)
        vm_net_mock_catalog_extra_apply_equip_to_shop(
            &g_vm_net_mock_equip_catalog_extra[i]);
}

static void vm_net_mock_catalog_extra_apply_to_item_effect_catalog(void)
{
    if (!vm_net_mock_catalog_extra_db_load() ||
        g_vm_net_mock_catalog_extra_effect_applied)
        return;
    g_vm_net_mock_catalog_extra_effect_applied = true;
    for (u32 i = 0; i < g_vm_net_mock_item_catalog_extra_count; ++i)
        vm_net_mock_catalog_extra_apply_item_to_effect(
            &g_vm_net_mock_item_catalog_extra[i]);
}

static void vm_net_mock_catalog_extra_apply_to_equipment_catalog(void)
{
    if (!vm_net_mock_catalog_extra_db_load() ||
        g_vm_net_mock_catalog_extra_equip_applied)
        return;
    g_vm_net_mock_catalog_extra_equip_applied = true;
    for (u32 i = 0; i < g_vm_net_mock_equip_catalog_extra_count; ++i)
        vm_net_mock_catalog_extra_apply_equip_to_stats(
            &g_vm_net_mock_equip_catalog_extra[i]);
}

static void vm_net_mock_catalog_extra_apply_to_skill_catalog(void)
{
    if (!vm_net_mock_catalog_extra_db_load() ||
        g_vm_net_mock_catalog_extra_skill_applied)
        return;
    g_vm_net_mock_catalog_extra_skill_applied = true;
    for (u32 i = 0; i < g_vm_net_mock_skill_catalog_extra_count; ++i)
        vm_net_mock_catalog_extra_apply_skill(
            &g_vm_net_mock_skill_catalog_extra[i]);
}

static u32 vm_net_mock_catalog_next_item_id(void)
{
    u32 maxId = 0;
    u32 shopTotal = 0;
    u32 effectTotal = 0;
    u32 equipTotal = 0;

    (void)vm_net_mock_catalog_extra_db_load();
    shopTotal = vm_net_mock_load_shop_catalog();
    effectTotal = vm_net_mock_load_item_effect_catalog();
    equipTotal = vm_net_mock_load_equipment_catalog();
    for (u32 i = 0; i < shopTotal; ++i)
    {
        if (g_vm_net_mock_shop_catalog[i].itemId > maxId)
            maxId = g_vm_net_mock_shop_catalog[i].itemId;
    }
    for (u32 i = 0; i < effectTotal; ++i)
    {
        if (g_vm_net_mock_item_effect_catalog[i].itemId > maxId)
            maxId = g_vm_net_mock_item_effect_catalog[i].itemId;
    }
    for (u32 i = 0; i < equipTotal; ++i)
    {
        if (g_vm_net_mock_equipment_catalog[i].itemId > maxId)
            maxId = g_vm_net_mock_equipment_catalog[i].itemId;
    }
    for (u32 i = 0; i < g_vm_net_mock_item_catalog_extra_count; ++i)
    {
        if (g_vm_net_mock_item_catalog_extra[i].itemId > maxId)
            maxId = g_vm_net_mock_item_catalog_extra[i].itemId;
    }
    for (u32 i = 0; i < g_vm_net_mock_equip_catalog_extra_count; ++i)
    {
        if (g_vm_net_mock_equip_catalog_extra[i].itemId > maxId)
            maxId = g_vm_net_mock_equip_catalog_extra[i].itemId;
    }
    if (maxId >= 0xfffffffeu)
        return 0;
    return maxId + 1u;
}

static u32 vm_net_mock_catalog_next_skill_id(void)
{
    u32 maxId = 0;
    u32 total = 0;

    (void)vm_net_mock_catalog_extra_db_load();
    total = vm_net_mock_load_skill_catalog();
    for (u32 i = 0; i < total; ++i)
    {
        if (g_vm_net_mock_skill_catalog[i].skillId > maxId)
            maxId = g_vm_net_mock_skill_catalog[i].skillId;
    }
    for (u32 i = 0; i < g_vm_net_mock_skill_catalog_extra_count; ++i)
    {
        if (g_vm_net_mock_skill_catalog_extra[i].skillId > maxId)
            maxId = g_vm_net_mock_skill_catalog_extra[i].skillId;
    }
    if (maxId >= 0xfffffffeu)
        return 0;
    return maxId + 1u;
}

static bool vm_net_mock_catalog_extra_item_in_use(u32 itemId)
{
    char query[256];
    u32 count = 0;

    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM account_role_backpack WHERE item_id=%u",
             itemId);
    if (vm_net_mock_catalog_extra_sql_count(query, &count) && count > 0)
        return true;
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM account_role_equipment WHERE item_id=%u",
             itemId);
    if (vm_net_mock_catalog_extra_sql_count(query, &count) && count > 0)
        return true;
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM account_role_warehouse WHERE item_id=%u",
             itemId);
    if (vm_net_mock_catalog_extra_sql_count(query, &count) && count > 0)
        return true;
    return false;
}

static bool vm_net_mock_catalog_extra_skill_in_use(u32 skillId)
{
    char query[256];
    u32 count = 0;

    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM account_role_skills WHERE skill_id=%u",
             skillId);
    return vm_net_mock_catalog_extra_sql_count(query, &count) && count > 0;
}

static void vm_net_mock_catalog_extra_remove_shop_item(u32 itemId)
{
    for (u32 i = 0; i < g_vm_net_mock_shop_catalog_count; ++i)
    {
        if (g_vm_net_mock_shop_catalog[i].itemId != itemId)
            continue;
        if (i + 1u < g_vm_net_mock_shop_catalog_count)
        {
            memmove(&g_vm_net_mock_shop_catalog[i],
                    &g_vm_net_mock_shop_catalog[i + 1],
                    (g_vm_net_mock_shop_catalog_count - i - 1u) *
                        sizeof(g_vm_net_mock_shop_catalog[0]));
        }
        --g_vm_net_mock_shop_catalog_count;
        return;
    }
}

static void vm_net_mock_catalog_extra_remove_effect_item(u32 itemId)
{
    for (u32 i = 0; i < g_vm_net_mock_item_effect_catalog_count; ++i)
    {
        if (g_vm_net_mock_item_effect_catalog[i].itemId != itemId)
            continue;
        if (i + 1u < g_vm_net_mock_item_effect_catalog_count)
        {
            memmove(&g_vm_net_mock_item_effect_catalog[i],
                    &g_vm_net_mock_item_effect_catalog[i + 1],
                    (g_vm_net_mock_item_effect_catalog_count - i - 1u) *
                        sizeof(g_vm_net_mock_item_effect_catalog[0]));
        }
        --g_vm_net_mock_item_effect_catalog_count;
        return;
    }
}

static void vm_net_mock_catalog_extra_remove_equipment_item(u32 itemId)
{
    for (u32 i = 0; i < g_vm_net_mock_equipment_catalog_count; ++i)
    {
        if (g_vm_net_mock_equipment_catalog[i].itemId != itemId)
            continue;
        if (i + 1u < g_vm_net_mock_equipment_catalog_count)
        {
            memmove(&g_vm_net_mock_equipment_catalog[i],
                    &g_vm_net_mock_equipment_catalog[i + 1],
                    (g_vm_net_mock_equipment_catalog_count - i - 1u) *
                        sizeof(g_vm_net_mock_equipment_catalog[0]));
        }
        --g_vm_net_mock_equipment_catalog_count;
        return;
    }
}

static void vm_net_mock_catalog_extra_remove_skill_item(u32 skillId)
{
    for (u32 i = 0; i < g_vm_net_mock_skill_catalog_count; ++i)
    {
        if (g_vm_net_mock_skill_catalog[i].skillId != skillId)
            continue;
        if (i + 1u < g_vm_net_mock_skill_catalog_count)
        {
            memmove(&g_vm_net_mock_skill_catalog[i],
                    &g_vm_net_mock_skill_catalog[i + 1],
                    (g_vm_net_mock_skill_catalog_count - i - 1u) *
                        sizeof(g_vm_net_mock_skill_catalog[0]));
        }
        --g_vm_net_mock_skill_catalog_count;
        return;
    }
}

static bool vm_net_mock_catalog_extra_update_shop_row(
    u32 itemId, const char *name, const char *description, u32 price,
    u32 npcPrice, u8 stack, u8 visual, bool equip, u8 category, bool enabled)
{
    vm_net_mock_shop_catalog_item *shop =
        vm_net_mock_find_shop_catalog_item_mut(itemId);
    u32 copyLen = 0;

    if (shop == NULL)
        return false;
    copyLen = vm_net_mock_shop_safe_name_len((const u8 *)name, (u32)strlen(name),
                                             VM_NET_MOCK_SHOP_NAME_BYTES);
    memset(shop->name, 0, sizeof(shop->name));
    if (copyLen > 0)
        memcpy(shop->name, name, copyLen);
    memset(shop->description, 0, sizeof(shop->description));
    if (description != NULL && description[0] != 0)
    {
        copyLen = vm_net_mock_shop_safe_name_len(
            (const u8 *)description, (u32)strlen(description),
            VM_NET_MOCK_SHOP_DESC_BYTES);
        memcpy(shop->description, description, copyLen);
    }
    shop->price = price ? price : VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE;
    shop->npcPrice = npcPrice ? npcPrice : shop->price;
    shop->stack = stack ? stack : 1;
    shop->stock = equip ? 1 : (stack ? stack : 99);
    shop->visual = visual ? visual : 1;
    shop->isEquip = equip ? 1 : 0;
    shop->category = category;
    shop->enabled = enabled ? 1u : 0u;
    return true;
}

static bool vm_net_mock_item_catalog_extra_create(
    vm_net_mock_item_catalog_extra_row *row, const char **errorOut)
{
    char query[2048];
    char nameHex[96];
    char descHex[512];
    u32 newId = 0;
    u32 attempt = 0;

    if (errorOut)
        *errorOut = "道具参数无效";
    if (row == NULL || row->name[0] == 0)
        return false;
    if (g_vm_net_mock_item_catalog_extra_count >= VM_NET_MOCK_CATALOG_EXTRA_MAX)
    {
        if (errorOut)
            *errorOut = "自定义道具数量已满";
        return false;
    }
    (void)vm_net_mock_load_shop_catalog();
    (void)vm_net_mock_load_item_effect_catalog();
    if (!vm_net_mock_catalog_extra_db_load())
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (vm_mysql_hex_encode(row->name, strlen(row->name), nameHex,
                            sizeof(nameHex)) == 0 ||
        vm_mysql_hex_encode(row->description, strlen(row->description), descHex,
                            sizeof(descHex)) == 0)
    {
        if (errorOut)
            *errorOut = "名称编码失败";
        return false;
    }
    for (attempt = 0; attempt < 8; ++attempt)
    {
        newId = vm_net_mock_catalog_next_item_id();
        if (newId == 0)
        {
            if (errorOut)
                *errorOut = "物品 ID 空间已满";
            return false;
        }
        if (vm_net_mock_find_shop_catalog_item(newId) != NULL ||
            vm_net_mock_find_item_effect_catalog_item(newId) != NULL ||
            vm_net_mock_find_equipment_catalog_item(newId) != NULL ||
            vm_net_mock_item_catalog_extra_index(newId) >= 0 ||
            vm_net_mock_equip_catalog_extra_index(newId) >= 0)
        {
            continue;
        }
        snprintf(query, sizeof(query),
                 "INSERT INTO server_item_catalog_extra("
                 "item_id,name,description,category,level_required,stack,"
                 "consume_mode,duration_minutes,hp,mp,exp,quest_link,bind_attr,"
                 "visual,price,cool_price,shop_enabled) VALUES("
                 "%u,UNHEX('%s'),UNHEX('%s'),%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
                 "%u,%u,%u,%u)",
                 newId, nameHex, descHex, row->category, row->levelRequired,
                 row->stack, row->consumeMode, row->durationMinutes, row->hp,
                 row->mp, row->exp, row->questLink, row->bindAttr, row->visual,
                 row->price, row->coolPrice, row->shopEnabled ? 1u : 0u);
        if (vm_mysql_exec(query))
            break;
        /* concurrent PK collision — retry with a fresh max+1 */
    }
    if (attempt >= 8)
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    row->itemId = newId;
    g_vm_net_mock_item_catalog_extra[g_vm_net_mock_item_catalog_extra_count++] =
        *row;
    vm_net_mock_catalog_extra_apply_item_to_shop(row);
    vm_net_mock_catalog_extra_apply_item_to_effect(row);
    vm_net_mock_catalog_extra_bump_rev();
    if (errorOut)
        *errorOut = NULL;
    return true;
}

static bool vm_net_mock_item_catalog_extra_save(
    const vm_net_mock_item_catalog_extra_row *row, const char **errorOut)
{
    char query[2048];
    char nameHex[96];
    char descHex[512];
    int index = -1;
    u32 price = 0;
    vm_net_mock_item_effect_catalog_item *effect = NULL;

    if (errorOut)
        *errorOut = "道具参数无效";
    if (row == NULL || row->itemId == 0 || row->name[0] == 0)
        return false;
    (void)vm_net_mock_catalog_extra_db_load();
    index = vm_net_mock_item_catalog_extra_index(row->itemId);
    if (index < 0)
    {
        if (errorOut)
            *errorOut = "不是自定义道具";
        return false;
    }
    if (vm_mysql_hex_encode(row->name, strlen(row->name), nameHex,
                            sizeof(nameHex)) == 0 ||
        vm_mysql_hex_encode(row->description, strlen(row->description), descHex,
                            sizeof(descHex)) == 0)
    {
        if (errorOut)
            *errorOut = "名称编码失败";
        return false;
    }
    snprintf(query, sizeof(query),
             "UPDATE server_item_catalog_extra SET "
             "name=UNHEX('%s'),description=UNHEX('%s'),category=%u,"
             "level_required=%u,stack=%u,consume_mode=%u,duration_minutes=%u,"
             "hp=%u,mp=%u,exp=%u,quest_link=%u,bind_attr=%u,visual=%u,"
             "price=%u,cool_price=%u,shop_enabled=%u WHERE item_id=%u",
             nameHex, descHex, row->category, row->levelRequired, row->stack,
             row->consumeMode, row->durationMinutes, row->hp, row->mp, row->exp,
             row->questLink, row->bindAttr, row->visual, row->price,
             row->coolPrice, row->shopEnabled ? 1u : 0u, row->itemId);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    g_vm_net_mock_item_catalog_extra[index] = *row;
    price = vm_net_mock_catalog_extra_shop_price(row);
    {
        u32 npcPrice = vm_net_mock_catalog_extra_npc_price(row, price);
        (void)vm_net_mock_catalog_extra_update_shop_row(
            row->itemId, row->name, row->description, price, npcPrice,
            row->stack ? row->stack : 99, row->visual ? row->visual : 1, false,
            row->category, row->shopEnabled != 0);
        effect = vm_net_mock_find_item_effect_catalog_item_mut(row->itemId);
        if (effect != NULL)
        {
            effect->category = row->category;
            effect->levelRequired = row->levelRequired;
            effect->stack = row->stack ? row->stack : 1;
            effect->consumeMode = row->consumeMode;
            effect->durationMinutes = row->durationMinutes;
            effect->hp = row->hp;
            effect->mp = row->mp;
            effect->exp = row->exp;
            effect->questLink = row->questLink;
            effect->bindAttr = row->bindAttr;
        }
        {
            const char *err = NULL;
            const vm_net_mock_shop_catalog_item *shop =
                vm_net_mock_find_shop_catalog_item(row->itemId);
            (void)vm_net_mock_shop_admin_save(
                row->itemId, price, npcPrice, row->shopEnabled != 0,
                shop != NULL && shop->npcEnabled != 0, &err);
        }
    }
    vm_net_mock_catalog_extra_bump_rev();
    if (errorOut)
        *errorOut = NULL;
    return true;
}

static bool vm_net_mock_item_catalog_extra_delete(u32 itemId,
                                                  const char **errorOut)
{
    char query[256];
    int index = -1;

    if (errorOut)
        *errorOut = "道具 ID 无效";
    if (itemId == 0)
        return false;
    (void)vm_net_mock_catalog_extra_db_load();
    index = vm_net_mock_item_catalog_extra_index(itemId);
    if (index < 0)
    {
        if (errorOut)
            *errorOut = "不是自定义道具";
        return false;
    }
    if (vm_net_mock_catalog_extra_item_in_use(itemId))
    {
        if (errorOut)
            *errorOut = "仍有角色持有该道具，无法删除";
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM server_item_catalog_extra WHERE item_id=%u", itemId);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM server_shop_items WHERE item_id=%u", itemId);
    (void)vm_mysql_exec(query);
    if ((u32)index + 1u < g_vm_net_mock_item_catalog_extra_count)
    {
        memmove(&g_vm_net_mock_item_catalog_extra[index],
                &g_vm_net_mock_item_catalog_extra[index + 1],
                (g_vm_net_mock_item_catalog_extra_count - (u32)index - 1u) *
                    sizeof(g_vm_net_mock_item_catalog_extra[0]));
    }
    --g_vm_net_mock_item_catalog_extra_count;
    vm_net_mock_catalog_extra_remove_shop_item(itemId);
    vm_net_mock_catalog_extra_remove_effect_item(itemId);
    vm_net_mock_catalog_extra_bump_rev();
    if (errorOut)
        *errorOut = NULL;
    return true;
}

static bool vm_net_mock_equip_catalog_extra_create(
    vm_net_mock_equip_catalog_extra_row *row, const char **errorOut)
{
    char query[2048];
    char nameHex[96];
    char descHex[512];
    u32 newId = 0;
    u32 attempt = 0;
    u8 slot = 0;

    if (errorOut)
        *errorOut = "装备参数无效";
    if (row == NULL || row->name[0] == 0)
        return false;
    slot = vm_net_mock_equipment_slot_for_category(row->category);
    if (slot >= VM_NET_MOCK_EQUIP_SLOT_COUNT)
    {
        if (errorOut)
            *errorOut = "装备类别无效";
        return false;
    }
    if (row->durabilityMax == 0)
        row->durabilityMax = 50;
    if (g_vm_net_mock_equip_catalog_extra_count >= VM_NET_MOCK_CATALOG_EXTRA_MAX)
    {
        if (errorOut)
            *errorOut = "自定义装备数量已满";
        return false;
    }
    (void)vm_net_mock_load_shop_catalog();
    (void)vm_net_mock_load_equipment_catalog();
    if (!vm_net_mock_catalog_extra_db_load())
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (vm_mysql_hex_encode(row->name, strlen(row->name), nameHex,
                            sizeof(nameHex)) == 0 ||
        vm_mysql_hex_encode(row->description, strlen(row->description), descHex,
                            sizeof(descHex)) == 0)
    {
        if (errorOut)
            *errorOut = "名称编码失败";
        return false;
    }
    for (attempt = 0; attempt < 8; ++attempt)
    {
        newId = vm_net_mock_catalog_next_item_id();
        if (newId == 0)
        {
            if (errorOut)
                *errorOut = "物品 ID 空间已满";
            return false;
        }
        if (vm_net_mock_find_shop_catalog_item(newId) != NULL ||
            vm_net_mock_find_equipment_catalog_item(newId) != NULL ||
            vm_net_mock_item_catalog_extra_index(newId) >= 0 ||
            vm_net_mock_equip_catalog_extra_index(newId) >= 0)
            continue;
        snprintf(query, sizeof(query),
                 "INSERT INTO server_equip_catalog_extra("
                 "item_id,name,description,category,level_required,quality,"
                 "durability_max,armor,attack,hp,mp,strength,agility,wisdom,"
                 "dodge,hit,crit,resist,price,shop_enabled) VALUES("
                 "%u,UNHEX('%s'),UNHEX('%s'),%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
                 "%u,%u,%u,%u,%u,%u)",
                 newId, nameHex, descHex, row->category, row->levelRequired,
                 row->quality, row->durabilityMax, row->bonus.armor,
                 row->bonus.attack, row->bonus.hp, row->bonus.mp,
                 row->bonus.strength, row->bonus.agility, row->bonus.wisdom,
                 row->bonus.dodge, row->bonus.hit, row->bonus.crit,
                 row->bonus.resist, row->price, row->shopEnabled ? 1u : 0u);
        if (vm_mysql_exec(query))
            break;
    }
    if (attempt >= 8)
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    row->itemId = newId;
    g_vm_net_mock_equip_catalog_extra[g_vm_net_mock_equip_catalog_extra_count++] =
        *row;
    vm_net_mock_catalog_extra_apply_equip_to_shop(row);
    vm_net_mock_catalog_extra_apply_equip_to_stats(row);
    vm_net_mock_catalog_extra_bump_rev();
    if (errorOut)
        *errorOut = NULL;
    return true;
}

static bool vm_net_mock_equip_catalog_extra_save(
    const vm_net_mock_equip_catalog_extra_row *row, const char **errorOut)
{
    char query[2048];
    char nameHex[96];
    char descHex[512];
    int index = -1;
    u32 price = 0;
    vm_net_mock_equipment_catalog_item *equip = NULL;
    u8 slot = 0;

    if (errorOut)
        *errorOut = "装备参数无效";
    if (row == NULL || row->itemId == 0 || row->name[0] == 0)
        return false;
    slot = vm_net_mock_equipment_slot_for_category(row->category);
    if (slot >= VM_NET_MOCK_EQUIP_SLOT_COUNT)
    {
        if (errorOut)
            *errorOut = "装备类别无效";
        return false;
    }
    (void)vm_net_mock_catalog_extra_db_load();
    index = vm_net_mock_equip_catalog_extra_index(row->itemId);
    if (index < 0)
    {
        if (errorOut)
            *errorOut = "不是自定义装备";
        return false;
    }
    if (vm_mysql_hex_encode(row->name, strlen(row->name), nameHex,
                            sizeof(nameHex)) == 0 ||
        vm_mysql_hex_encode(row->description, strlen(row->description), descHex,
                            sizeof(descHex)) == 0)
    {
        if (errorOut)
            *errorOut = "名称编码失败";
        return false;
    }
    snprintf(query, sizeof(query),
             "UPDATE server_equip_catalog_extra SET "
             "name=UNHEX('%s'),description=UNHEX('%s'),category=%u,"
             "level_required=%u,quality=%u,durability_max=%u,armor=%u,attack=%u,"
             "hp=%u,mp=%u,strength=%u,agility=%u,wisdom=%u,dodge=%u,hit=%u,"
             "crit=%u,resist=%u,price=%u,shop_enabled=%u WHERE item_id=%u",
             nameHex, descHex, row->category, row->levelRequired, row->quality,
             row->durabilityMax, row->bonus.armor, row->bonus.attack,
             row->bonus.hp, row->bonus.mp, row->bonus.strength,
             row->bonus.agility, row->bonus.wisdom, row->bonus.dodge,
             row->bonus.hit, row->bonus.crit, row->bonus.resist, row->price,
             row->shopEnabled ? 1u : 0u, row->itemId);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    g_vm_net_mock_equip_catalog_extra[index] = *row;
    price = row->price ? row->price : VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE;
    (void)vm_net_mock_catalog_extra_update_shop_row(
        row->itemId, row->name, row->description, price, price, 1, 1, true,
        row->category, row->shopEnabled != 0);
    equip = vm_net_mock_find_equipment_catalog_item_mut(row->itemId);
    if (equip != NULL)
    {
        equip->levelRequired = row->levelRequired;
        equip->quality = row->quality;
        equip->category = row->category;
        equip->slot = slot;
        equip->durabilityMax = row->durabilityMax ? row->durabilityMax : 50;
        equip->bonus = row->bonus;
    }
    {
        const char *err = NULL;
        const vm_net_mock_shop_catalog_item *shop =
            vm_net_mock_find_shop_catalog_item(row->itemId);
        (void)vm_net_mock_shop_admin_save(
            row->itemId, price, price, row->shopEnabled != 0,
            shop != NULL && shop->npcEnabled != 0, &err);
    }
    vm_net_mock_catalog_extra_bump_rev();
    if (errorOut)
        *errorOut = NULL;
    return true;
}

static bool vm_net_mock_equip_catalog_extra_delete(u32 itemId,
                                                   const char **errorOut)
{
    char query[256];
    int index = -1;

    if (errorOut)
        *errorOut = "装备 ID 无效";
    if (itemId == 0)
        return false;
    (void)vm_net_mock_catalog_extra_db_load();
    index = vm_net_mock_equip_catalog_extra_index(itemId);
    if (index < 0)
    {
        if (errorOut)
            *errorOut = "不是自定义装备";
        return false;
    }
    if (vm_net_mock_catalog_extra_item_in_use(itemId))
    {
        if (errorOut)
            *errorOut = "仍有角色持有该装备，无法删除";
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM server_equip_catalog_extra WHERE item_id=%u", itemId);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM server_shop_items WHERE item_id=%u", itemId);
    (void)vm_mysql_exec(query);
    if ((u32)index + 1u < g_vm_net_mock_equip_catalog_extra_count)
    {
        memmove(&g_vm_net_mock_equip_catalog_extra[index],
                &g_vm_net_mock_equip_catalog_extra[index + 1],
                (g_vm_net_mock_equip_catalog_extra_count - (u32)index - 1u) *
                    sizeof(g_vm_net_mock_equip_catalog_extra[0]));
    }
    --g_vm_net_mock_equip_catalog_extra_count;
    vm_net_mock_catalog_extra_remove_shop_item(itemId);
    vm_net_mock_catalog_extra_remove_equipment_item(itemId);
    vm_net_mock_catalog_extra_bump_rev();
    if (errorOut)
        *errorOut = NULL;
    return true;
}

static bool vm_net_mock_skill_catalog_extra_create(
    vm_net_mock_skill_catalog_extra_row *row, const char **errorOut)
{
    char query[2048];
    char nameHex[96];
    char descHex[512];
    u32 newId = 0;
    u32 attempt = 0;

    if (errorOut)
        *errorOut = "技能参数无效";
    if (row == NULL || row->name[0] == 0 || row->rawJob > 2)
        return false;
    if (g_vm_net_mock_skill_catalog_extra_count >= VM_NET_MOCK_CATALOG_EXTRA_MAX ||
        g_vm_net_mock_skill_catalog_count >= VM_NET_MOCK_SKILL_CATALOG_MAX_ITEMS)
    {
        if (errorOut)
            *errorOut = "技能目录已满";
        return false;
    }
    (void)vm_net_mock_load_skill_catalog();
    if (!vm_net_mock_catalog_extra_db_load())
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (vm_mysql_hex_encode(row->name, strlen(row->name), nameHex,
                            sizeof(nameHex)) == 0 ||
        vm_mysql_hex_encode(row->description, strlen(row->description), descHex,
                            sizeof(descHex)) == 0)
    {
        if (errorOut)
            *errorOut = "名称编码失败";
        return false;
    }
    for (attempt = 0; attempt < 8; ++attempt)
    {
        newId = vm_net_mock_catalog_next_skill_id();
        if (newId == 0)
        {
            if (errorOut)
                *errorOut = "技能 ID 空间已满";
            return false;
        }
        if (vm_net_mock_find_skill_catalog_item(newId) != NULL ||
            vm_net_mock_skill_catalog_extra_index(newId) >= 0)
            continue;
        snprintf(query, sizeof(query),
                 "INSERT INTO server_skill_catalog_extra("
                 "skill_id,name,description,effect_index,level_required,raw_job,"
                 "learn_price,use_limit,target_direction,mp_cost,hp_change,"
                 "strength_coeff,agility_coeff,wisdom_coeff,duration_rounds,"
                 "strength_change,agility_change,wisdom_change,attack_change,"
                 "defense_change,crit_change,hit_change,dodge_change,"
                 "resist_change,effect_kind) VALUES("
                 "%u,UNHEX('%s'),UNHEX('%s'),%u,%u,%u,%u,%u,%u,%u,%d,"
                 "%u,%u,%u,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u)",
                 newId, nameHex, descHex, row->effectIndex, row->levelRequired,
                 row->rawJob, row->learnPrice, row->useLimit,
                 row->targetDirection, row->mpCost, row->hpChange,
                 row->strengthCoeff, row->agilityCoeff, row->wisdomCoeff,
                 row->durationRounds, row->strengthChange, row->agilityChange,
                 row->wisdomChange, row->attackChange, row->defenseChange,
                 row->critChange, row->hitChange, row->dodgeChange,
                 row->resistChange, row->effectKind);
        if (vm_mysql_exec(query))
            break;
    }
    if (attempt >= 8)
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    row->skillId = newId;
    g_vm_net_mock_skill_catalog_extra[g_vm_net_mock_skill_catalog_extra_count++] =
        *row;
    vm_net_mock_catalog_extra_apply_skill(row);
    vm_net_mock_catalog_extra_bump_rev();
    if (errorOut)
        *errorOut = NULL;
    return true;
}

static bool vm_net_mock_skill_catalog_extra_save(
    const vm_net_mock_skill_catalog_extra_row *row, const char **errorOut)
{
    char query[2048];
    char nameHex[96];
    char descHex[512];
    int index = -1;
    vm_net_mock_skill_catalog_item *skill = NULL;
    u32 copyLen = 0;

    if (errorOut)
        *errorOut = "技能参数无效";
    if (row == NULL || row->skillId == 0 || row->name[0] == 0 || row->rawJob > 2)
        return false;
    (void)vm_net_mock_catalog_extra_db_load();
    index = vm_net_mock_skill_catalog_extra_index(row->skillId);
    if (index < 0)
    {
        if (errorOut)
            *errorOut = "不是自定义技能";
        return false;
    }
    if (vm_mysql_hex_encode(row->name, strlen(row->name), nameHex,
                            sizeof(nameHex)) == 0 ||
        vm_mysql_hex_encode(row->description, strlen(row->description), descHex,
                            sizeof(descHex)) == 0)
    {
        if (errorOut)
            *errorOut = "名称编码失败";
        return false;
    }
    snprintf(query, sizeof(query),
             "UPDATE server_skill_catalog_extra SET "
             "name=UNHEX('%s'),description=UNHEX('%s'),effect_index=%u,"
             "level_required=%u,raw_job=%u,learn_price=%u,use_limit=%u,"
             "target_direction=%u,mp_cost=%u,hp_change=%d,strength_coeff=%u,"
             "agility_coeff=%u,wisdom_coeff=%u,duration_rounds=%u,"
             "strength_change=%d,agility_change=%d,wisdom_change=%d,"
             "attack_change=%d,defense_change=%d,crit_change=%d,hit_change=%d,"
             "dodge_change=%d,resist_change=%d,effect_kind=%u WHERE skill_id=%u",
             nameHex, descHex, row->effectIndex, row->levelRequired, row->rawJob,
             row->learnPrice, row->useLimit, row->targetDirection, row->mpCost,
             row->hpChange, row->strengthCoeff, row->agilityCoeff,
             row->wisdomCoeff, row->durationRounds, row->strengthChange,
             row->agilityChange, row->wisdomChange, row->attackChange,
             row->defenseChange, row->critChange, row->hitChange,
             row->dodgeChange, row->resistChange, row->effectKind, row->skillId);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    g_vm_net_mock_skill_catalog_extra[index] = *row;
    skill = vm_net_mock_find_skill_catalog_item_mut(row->skillId);
    if (skill != NULL)
    {
        skill->effectIndex = row->effectIndex;
        skill->learnPrice = row->learnPrice;
        skill->mpCost = row->mpCost;
        skill->hpChange = row->hpChange;
        skill->strengthCoeff = row->strengthCoeff;
        skill->agilityCoeff = row->agilityCoeff;
        skill->wisdomCoeff = row->wisdomCoeff;
        skill->rawJob = row->rawJob;
        skill->levelRequired = row->levelRequired;
        skill->targetDirection = row->targetDirection;
        skill->useLimit = row->useLimit;
        skill->durationRounds = row->durationRounds;
        skill->strengthChange = row->strengthChange;
        skill->agilityChange = row->agilityChange;
        skill->wisdomChange = row->wisdomChange;
        skill->attackChange = row->attackChange;
        skill->defenseChange = row->defenseChange;
        skill->critChange = row->critChange;
        skill->hitChange = row->hitChange;
        skill->dodgeChange = row->dodgeChange;
        skill->resistChange = row->resistChange;
        skill->effectKind = row->effectKind;
        copyLen = vm_net_mock_shop_safe_name_len(
            (const u8 *)row->name, (u32)strlen(row->name),
            VM_NET_MOCK_SKILL_NAME_BYTES);
        memset(skill->name, 0, sizeof(skill->name));
        if (copyLen > 0)
            memcpy(skill->name, row->name, copyLen);
    }
    vm_net_mock_catalog_extra_bump_rev();
    if (errorOut)
        *errorOut = NULL;
    return true;
}

static bool vm_net_mock_skill_catalog_extra_delete(u32 skillId,
                                                   const char **errorOut)
{
    char query[256];
    int index = -1;

    if (errorOut)
        *errorOut = "技能 ID 无效";
    if (skillId == 0)
        return false;
    (void)vm_net_mock_catalog_extra_db_load();
    index = vm_net_mock_skill_catalog_extra_index(skillId);
    if (index < 0)
    {
        if (errorOut)
            *errorOut = "不是自定义技能";
        return false;
    }
    if (vm_net_mock_catalog_extra_skill_in_use(skillId))
    {
        if (errorOut)
            *errorOut = "仍有角色已学习该技能，无法删除";
        return false;
    }
    snprintf(query, sizeof(query),
             "DELETE FROM server_skill_catalog_extra WHERE skill_id=%u", skillId);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if ((u32)index + 1u < g_vm_net_mock_skill_catalog_extra_count)
    {
        memmove(&g_vm_net_mock_skill_catalog_extra[index],
                &g_vm_net_mock_skill_catalog_extra[index + 1],
                (g_vm_net_mock_skill_catalog_extra_count - (u32)index - 1u) *
                    sizeof(g_vm_net_mock_skill_catalog_extra[0]));
    }
    --g_vm_net_mock_skill_catalog_extra_count;
    vm_net_mock_catalog_extra_remove_skill_item(skillId);
    vm_net_mock_catalog_extra_bump_rev();
    if (errorOut)
        *errorOut = NULL;
    return true;
}
