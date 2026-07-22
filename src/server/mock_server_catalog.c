static u8 vm_net_mock_role_backpack_count(const vm_net_mock_role_state *role)
{
    u32 count = 0;
    if (role == NULL)
        return 0;
    count = role->backpackItemCount;
    if (count > VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        count = VM_NET_MOCK_BACKPACK_MAX_ITEMS;
    if (count > role->backpackCapacity)
        count = role->backpackCapacity;
    return (u8)count;
}

static bool vm_net_mock_backpack_item_id_uses_reservoir_count(u32 itemId)
{
    /*
     * These are the two hard-coded special cases in
     * mmGameMstarWqvga.cbm:0x00000D04.  Their persisted item_count is a
     * 32-bit HP/MP reservoir, while their visible stack quantity is always 1.
     */
    return itemId == 802 || itemId == 803;
}

static u8 vm_net_mock_backpack_stack_byte(const vm_net_mock_backpack_item_state *item)
{
    if (item == NULL || item->count == 0)
        return 0;
    if (vm_net_mock_backpack_item_id_uses_reservoir_count(item->itemId))
        return 1;
    return item->count > 255 ? 255 : (u8)item->count;
}

static u32 vm_net_mock_backpack_grid_wire_count(const vm_net_mock_backpack_item_state *item)
{
    if (item == NULL)
        return 0;
    /*
     * JianghuOL.CBE:0x01039952 passes this value to 0x0101918E, which stores
     * it in the item's 16-bit quantity slot at +242.  The full reservoir is
     * restored separately through 7/11 after the grid row exists.
     */
    if (vm_net_mock_backpack_item_id_uses_reservoir_count(item->itemId))
        return item->count == 0 ? 0 : 1;
    return item->count;
}

typedef struct
{
    u32 itemId;
    char name[VM_NET_MOCK_SHOP_NAME_BYTES + 1];
    u32 price;
    u32 stock;
    u8 stack;
    u8 visual;
    u8 isEquip;
    u8 category;
    u8 enabled;
} vm_net_mock_shop_catalog_item;

typedef struct
{
    u32 itemId;
    u8 slot;
    u8 levelRequired;
    u16 reserved0;
    vm_net_mock_equipment_bonus bonus;
} vm_net_mock_equipment_catalog_item;

typedef struct
{
    u32 itemId;
    u8 category;
    u8 levelRequired;
    u8 stack;
    u8 consumeMode;
    u32 hp;
    u32 mp;
    u32 exp;
} vm_net_mock_item_effect_catalog_item;

typedef struct
{
    u32 skillId;
    u32 effectIndex;
    u32 learnPrice;
    u32 mpCost;
    int32_t hpChange;
    u32 strengthCoeff;
    u32 agilityCoeff;
    u32 wisdomCoeff;
    u8 rawJob;
    u8 levelRequired;
    char name[VM_NET_MOCK_SKILL_NAME_BYTES + 1];
} vm_net_mock_skill_catalog_item;

typedef struct
{
    char scene[64];
    u32 monsterIds[3];
} vm_net_mock_auto_monster_catalog_item;

static vm_net_mock_shop_catalog_item g_vm_net_mock_shop_catalog[VM_NET_MOCK_SHOP_MAX_CATALOG_ITEMS];
static u32 g_vm_net_mock_shop_catalog_count = 0;
static bool g_vm_net_mock_shop_catalog_loaded = false;
static bool g_vm_net_mock_shop_admin_db_loaded = false;
static bool g_vm_net_mock_shop_admin_db_valid = false;
static vm_net_mock_equipment_catalog_item g_vm_net_mock_equipment_catalog[VM_NET_MOCK_EQUIP_CATALOG_MAX_ITEMS];
static u32 g_vm_net_mock_equipment_catalog_count = 0;
static bool g_vm_net_mock_equipment_catalog_loaded = false;
static vm_net_mock_item_effect_catalog_item g_vm_net_mock_item_effect_catalog[VM_NET_MOCK_ITEM_EFFECT_CATALOG_MAX_ITEMS];
static u32 g_vm_net_mock_item_effect_catalog_count = 0;
static bool g_vm_net_mock_item_effect_catalog_loaded = false;
static vm_net_mock_skill_catalog_item g_vm_net_mock_skill_catalog[VM_NET_MOCK_SKILL_CATALOG_MAX_ITEMS];
static u32 g_vm_net_mock_skill_catalog_count = 0;
static bool g_vm_net_mock_skill_catalog_loaded = false;
static vm_net_mock_auto_monster_catalog_item g_vm_net_mock_auto_monster_catalog[VM_NET_MOCK_AUTO_MONSTER_CATALOG_MAX_ITEMS];
static u32 g_vm_net_mock_auto_monster_catalog_count = 0;
static bool g_vm_net_mock_auto_monster_catalog_loaded = false;
static bool g_vm_net_mock_eidolon_catalog_loaded = false;
static bool g_vm_net_mock_eidolon_heal_effect_found = false;
static u32 g_vm_net_mock_eidolon_heal_effect_index = 0;

typedef struct
{
    bool used;
    bool loaded;
    char accountId[64];
    u32 roleId;
    u32 equipmentItemIds[VM_NET_MOCK_EQUIP_SLOT_COUNT];
    u16 durability[VM_NET_MOCK_EQUIP_SLOT_COUNT];
    u16 durabilityMax[VM_NET_MOCK_EQUIP_SLOT_COUNT];
    u32 lastBattleWearSerial;
    u8 learnedSkillCount;
    u32 learnedSkillIds[VM_NET_MOCK_LEARNED_SKILL_MAX_ITEMS];
} vm_net_mock_role_service_state;

static vm_net_mock_role_service_state
    g_vm_net_mock_role_service_states[VM_NET_MOCK_ROLE_SERVICE_CACHE_MAX];
static u32 g_vm_net_mock_role_service_state_replace_index = 0;
static bool g_vm_net_mock_role_service_tables_checked = false;
static bool g_vm_net_mock_role_service_tables_valid = false;

static bool vm_mock_mysql_parse_u32(const char *value, size_t value_len,
                                    u32 *result_out);
static bool vm_net_mock_shop_admin_db_load(void);

static u32 vm_net_mock_shop_catalog_group(u32 itemId)
{
    if (itemId >= 1000)
        return 0;
    if (itemId >= 800 && itemId < 1000)
        return 1;
    return 2;
}

static int vm_net_mock_compare_shop_catalog_items(const void *lhs, const void *rhs)
{
    const vm_net_mock_shop_catalog_item *a = (const vm_net_mock_shop_catalog_item *)lhs;
    const vm_net_mock_shop_catalog_item *b = (const vm_net_mock_shop_catalog_item *)rhs;
    u32 groupA = vm_net_mock_shop_catalog_group(a->itemId);
    u32 groupB = vm_net_mock_shop_catalog_group(b->itemId);

    if (groupA != groupB)
        return groupA < groupB ? -1 : 1;
    if (a->itemId != b->itemId)
        return a->itemId < b->itemId ? -1 : 1;
    return 0;
}

static void vm_net_mock_sort_shop_catalog(void)
{
    if (g_vm_net_mock_shop_catalog_count > 1)
    {
        qsort(g_vm_net_mock_shop_catalog,
              g_vm_net_mock_shop_catalog_count,
              sizeof(g_vm_net_mock_shop_catalog[0]),
              vm_net_mock_compare_shop_catalog_items);
    }
}

static u32 vm_net_mock_shop_safe_name_len(const u8 *name, u32 nameLen, u32 cap)
{
    u32 pos = 0;
    if (name == NULL)
        return 0;
    while (pos < nameLen && pos < cap)
    {
        if (name[pos] < 0x80)
        {
            ++pos;
        }
        else if (pos + 1 < nameLen && pos + 2 <= cap)
        {
            pos += 2;
        }
        else
        {
            break;
        }
    }
    return pos;
}

static u32 vm_net_mock_read_le32_at(const u8 *data, u32 off)
{
    return (u32)data[off] |
           ((u32)data[off + 1] << 8) |
           ((u32)data[off + 2] << 16) |
           ((u32)data[off + 3] << 24);
}

static u32 vm_net_mock_parse_dsh_u32(const u8 *raw, u32 len, u32 fallback)
{
    u32 value = 0;
    bool haveDigit = false;

    if (raw == NULL || len == 0)
        return fallback;
    for (u32 i = 0; i < len; ++i)
    {
        if (raw[i] == '-' && !haveDigit)
            return fallback;
        if (raw[i] < '0' || raw[i] > '9')
            break;
        haveDigit = true;
        value = value * 10u + (u32)(raw[i] - '0');
    }
    return haveDigit ? value : fallback;
}

static int32_t vm_net_mock_parse_dsh_s32(const u8 *raw, u32 len, int32_t fallback)
{
    int32_t sign = 1;
    int32_t value = 0;
    bool haveDigit = false;
    u32 pos = 0;

    if (raw == NULL || len == 0)
        return fallback;
    if (raw[pos] == '-')
    {
        sign = -1;
        ++pos;
    }
    else if (raw[pos] == '+')
    {
        ++pos;
    }
    for (; pos < len; ++pos)
    {
        if (raw[pos] < '0' || raw[pos] > '9')
            break;
        haveDigit = true;
        value = value * 10 + (int32_t)(raw[pos] - '0');
    }
    return haveDigit ? value * sign : fallback;
}

static bool vm_net_mock_dsh_value_equals_ascii(const u8 *raw, u32 len,
                                               const char *text)
{
    size_t textLen = text ? strlen(text) : 0;

    if (raw == NULL || text == NULL || textLen != len)
        return false;
    return memcmp(raw, text, len) == 0;
}

static u8 vm_net_mock_role_job_to_skill_raw_job(u8 roleJob)
{
    if (roleJob >= 1 && roleJob <= 3)
        return (u8)(roleJob - 1);
    return 0;
}

static const char *vm_net_mock_skill_raw_job_name(u8 rawJob)
{
    switch (rawJob)
    {
    case 0:
        return "Tianji";
    case 1:
        return "Huanjian";
    case 2:
        return "Guidao";
    default:
        return "Unknown";
    }
}

static int vm_net_mock_compare_skill_catalog_items(const void *lhs, const void *rhs)
{
    const vm_net_mock_skill_catalog_item *a = (const vm_net_mock_skill_catalog_item *)lhs;
    const vm_net_mock_skill_catalog_item *b = (const vm_net_mock_skill_catalog_item *)rhs;

    if (a->rawJob != b->rawJob)
        return a->rawJob < b->rawJob ? -1 : 1;
    if (a->levelRequired != b->levelRequired)
        return a->levelRequired < b->levelRequired ? -1 : 1;
    if (a->skillId != b->skillId)
        return a->skillId < b->skillId ? -1 : 1;
    return 0;
}

static void vm_net_mock_sort_skill_catalog(void)
{
    if (g_vm_net_mock_skill_catalog_count > 1)
    {
        qsort(g_vm_net_mock_skill_catalog,
              g_vm_net_mock_skill_catalog_count,
              sizeof(g_vm_net_mock_skill_catalog[0]),
              vm_net_mock_compare_skill_catalog_items);
    }
}

static bool vm_net_mock_add_skill_catalog_item(u32 skillId, u32 rawJob,
                                               u32 levelRequired,
                                               u32 effectIndex,
                                               u32 learnPrice,
                                               u32 mpCost,
                                               int32_t hpChange,
                                               u32 strengthCoeff,
                                               u32 agilityCoeff,
                                               u32 wisdomCoeff,
                                               const u8 *name,
                                               u32 nameLen)
{
    vm_net_mock_skill_catalog_item *skill = NULL;
    u32 copyLen = 0;

    if (skillId == 0 ||
        rawJob > 2 ||
        g_vm_net_mock_skill_catalog_count >= VM_NET_MOCK_SKILL_CATALOG_MAX_ITEMS)
    {
        return false;
    }

    skill = &g_vm_net_mock_skill_catalog[g_vm_net_mock_skill_catalog_count++];
    memset(skill, 0, sizeof(*skill));
    skill->skillId = skillId;
    skill->effectIndex = effectIndex;
    skill->learnPrice = learnPrice;
    skill->mpCost = mpCost;
    skill->hpChange = hpChange;
    skill->strengthCoeff = strengthCoeff;
    skill->agilityCoeff = agilityCoeff;
    skill->wisdomCoeff = wisdomCoeff;
    skill->rawJob = (u8)rawJob;
    skill->levelRequired = (u8)((levelRequired == 0) ? 1 :
                                (levelRequired > 255 ? 255 : levelRequired));
    copyLen = vm_net_mock_shop_safe_name_len(name, nameLen, VM_NET_MOCK_SKILL_NAME_BYTES);
    if (copyLen > 0)
        memcpy(skill->name, name, copyLen);
    skill->name[copyLen] = 0;
    return true;
}

static bool vm_net_mock_load_eidolon_effect_index_dsh(const char *path,
                                                      const char *actorName,
                                                      u32 *indexOut)
{
    static u8 data[4096];
    u32 len = vm_net_mock_load_response_file(path, data, sizeof(data));
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 pos = 16;

    if (indexOut)
        *indexOut = 0;
    if (actorName == NULL || indexOut == NULL || len < 16)
        return false;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    if (columnCount < 2 || columnCount > 16 || rowCount > 1024)
        return false;

    for (u32 i = 0; i < columnCount; ++i)
    {
        u32 fieldLen = 0;
        if (pos >= len)
            return false;
        fieldLen = data[pos++];
        if (pos + fieldLen > len)
            return false;
        pos += fieldLen;
    }

    for (u32 row = 0; row < rowCount && pos + 4 <= len; ++row)
    {
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowPos = pos + 4;
        u32 rowEnd = rowPos + rowLen;
        u32 sequence = 0;
        bool haveSequence = false;
        const u8 *name = NULL;
        u32 nameLen = 0;

        if (rowEnd > len || rowEnd < rowPos)
            break;

        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;

            if (rowPos + valueLen > rowEnd)
                break;
            switch (col)
            {
            case 0:
                sequence = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                haveSequence = valueLen != 0;
                break;
            case 1:
                name = value;
                nameLen = valueLen;
                break;
            default:
                break;
            }
            rowPos += valueLen;
        }

        if (haveSequence &&
            vm_net_mock_dsh_value_equals_ascii(name, nameLen, actorName))
        {
            *indexOut = sequence;
            return true;
        }
        pos = rowEnd;
    }
    return false;
}

static bool vm_net_mock_eidolon_heal_effect_index(u32 *indexOut)
{
    if (indexOut)
        *indexOut = 0;
    if (!g_vm_net_mock_eidolon_catalog_loaded)
    {
        g_vm_net_mock_eidolon_catalog_loaded = true;
        g_vm_net_mock_eidolon_heal_effect_found =
            vm_net_mock_load_eidolon_effect_index_dsh("JHOnlineData/eidolon.dsh",
                                                      "f_renew1.actor",
                                                      &g_vm_net_mock_eidolon_heal_effect_index);
        if (!g_vm_net_mock_eidolon_heal_effect_found)
        {
            g_vm_net_mock_eidolon_heal_effect_found =
                vm_net_mock_load_eidolon_effect_index_dsh("bin/JHOnlineData/eidolon.dsh",
                                                          "f_renew1.actor",
                                                          &g_vm_net_mock_eidolon_heal_effect_index);
        }
        if (g_vm_net_mock_eidolon_heal_effect_found)
        {
            printf("[info][network] mock_eidolon_effect actor=f_renew1.actor index=%u source=eidolon.dsh\n",
                   g_vm_net_mock_eidolon_heal_effect_index);
        }
        else
        {
            printf("[warn][network] mock_eidolon_effect actor=f_renew1.actor missing source=eidolon.dsh\n");
        }
    }
    if (!g_vm_net_mock_eidolon_heal_effect_found)
        return false;
    if (indexOut)
        *indexOut = g_vm_net_mock_eidolon_heal_effect_index;
    return true;
}

static u32 vm_net_mock_load_skill_catalog_dsh(const char *path)
{
    static u8 data[32768];
    u32 len = vm_net_mock_load_response_file(path, data, sizeof(data));
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 pos = 16;
    u32 added = 0;

    if (len < 16)
        return 0;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    if (columnCount == 0 || columnCount > 64 || rowCount > 10000)
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
        u32 skillId = 0;
        u32 rawJob = 0xff;
        u32 levelRequired = 1;
        u32 effectIndex = 0;
        u32 learnPrice = 0;
        u32 mpCost = 0;
        int32_t hpChange = 0;
        u32 strengthCoeff = 0;
        u32 agilityCoeff = 0;
        u32 wisdomCoeff = 0;
        const u8 *name = NULL;
        u32 nameLen = 0;

        if (rowEnd > len || rowEnd < rowPos)
            break;

        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;

            if (rowPos + valueLen > rowEnd)
                break;
            switch (col)
            {
            case 0:
                skillId = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                break;
            case 1:
                name = value;
                nameLen = valueLen;
                break;
            case 2:
                /* Battle action effect index; maps to eidolon.dsh sequence. */
                effectIndex = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                break;
            case 4:
                levelRequired = vm_net_mock_parse_dsh_u32(value, valueLen, 1);
                break;
            case 6:
                rawJob = vm_net_mock_parse_dsh_u32(value, valueLen, 0xff);
                break;
            case 7:
                /* skill.dsh `价值`: copper charged by the skill trainer. */
                learnPrice = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                break;
            case 12:
                mpCost = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                break;
            case 14:
                hpChange = vm_net_mock_parse_dsh_s32(value, valueLen, 0);
                break;
            case 29:
                strengthCoeff = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                break;
            case 30:
                agilityCoeff = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                break;
            case 31:
                wisdomCoeff = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                break;
            default:
                break;
            }
            rowPos += valueLen;
        }

        if (vm_net_mock_add_skill_catalog_item(skillId, rawJob, levelRequired,
                                               effectIndex,
                                               learnPrice,
                                               mpCost,
                                               hpChange,
                                               strengthCoeff,
                                               agilityCoeff,
                                               wisdomCoeff,
                                               name, nameLen))
        {
            ++added;
        }
        pos = rowEnd;
    }

    return added;
}

static u32 vm_net_mock_load_skill_catalog(void)
{
    u32 skillCount = 0;

    if (g_vm_net_mock_skill_catalog_loaded)
        return g_vm_net_mock_skill_catalog_count;

    g_vm_net_mock_skill_catalog_loaded = true;
    g_vm_net_mock_skill_catalog_count = 0;
    skillCount = vm_net_mock_load_skill_catalog_dsh("JHOnlineData/skill.dsh");
    if (skillCount == 0)
        skillCount = vm_net_mock_load_skill_catalog_dsh("bin/JHOnlineData/skill.dsh");

    if (skillCount == 0)
    {
        (void)vm_net_mock_add_skill_catalog_item(1, 0, 1, 14, 50, 10,
                                                -130, 50, 0, 0,
                                                (const u8 *)"\xcd\xf2\xbd\xa3\xd6\xef\xcf\xc9\x31",
                                                9);
        (void)vm_net_mock_add_skill_catalog_item(101, 1, 1, 1, 50, 20,
                                                -75, 0, 50, 0,
                                                (const u8 *)"\xb7\xe7\xce\xe8\xc8\xd0\xd0\xd0\x31",
                                                9);
        (void)vm_net_mock_add_skill_catalog_item(201, 2, 1, 7, 50, 5,
                                                -30, 0, 0, 110,
                                                (const u8 *)"\xe7\xca\xd1\xd7\xbb\xc3\xb7\xa8\x31",
                                                9);
        printf("[warn][network] mock_skill_catalog fallback=skill.dsh-not-found total=%u\n",
               g_vm_net_mock_skill_catalog_count);
    }
    else
    {
        vm_net_mock_sort_skill_catalog();
        printf("[info][network] mock_skill_catalog total=%u source=skill.dsh\n",
               g_vm_net_mock_skill_catalog_count);
    }
    return g_vm_net_mock_skill_catalog_count;
}

static const vm_net_mock_skill_catalog_item *vm_net_mock_find_skill_catalog_item(u32 skillId)
{
    u32 total = vm_net_mock_load_skill_catalog();

    for (u32 i = 0; i < total; ++i)
    {
        if (g_vm_net_mock_skill_catalog[i].skillId == skillId)
            return &g_vm_net_mock_skill_catalog[i];
    }
    return NULL;
}

static const vm_net_mock_skill_catalog_item *vm_net_mock_battle_operate_skill(u32 operate);

typedef struct
{
    vm_net_mock_role_service_state *state;
    bool invalid;
} vm_net_mock_role_service_load_context;

static bool vm_net_mock_role_service_tables_ensure(void)
{
    if (g_vm_net_mock_role_service_tables_checked)
        return g_vm_net_mock_role_service_tables_valid;
    g_vm_net_mock_role_service_tables_checked = true;
    g_vm_net_mock_role_service_tables_valid =
        vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_role_equipment_durability ("
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,slot_index TINYINT UNSIGNED NOT NULL,"
            "item_id INT UNSIGNED NOT NULL DEFAULT 0,"
            "durability SMALLINT UNSIGNED NOT NULL DEFAULT 100,"
            "durability_max SMALLINT UNSIGNED NOT NULL DEFAULT 100,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(account_id,role_id,slot_index)) ENGINE=InnoDB") &&
        vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS account_role_skills ("
            "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
            "role_id INT UNSIGNED NOT NULL,skill_id INT UNSIGNED NOT NULL,"
            "skill_level SMALLINT UNSIGNED NOT NULL DEFAULT 1,"
            "learned_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY(account_id,role_id,skill_id)) ENGINE=InnoDB");
    if (!g_vm_net_mock_role_service_tables_valid)
    {
        printf("[error][network] mock_role_service_schema error=%s\n",
               vm_mysql_last_error());
    }
    return g_vm_net_mock_role_service_tables_valid;
}

static bool vm_net_mock_role_service_account_hex(const char *accountId,
                                                  char out[129])
{
    size_t accountLen = accountId ? strlen(accountId) : 0;
    return accountLen > 0 && accountLen < 64 &&
           vm_mysql_hex_encode(accountId, accountLen, out, 129) != 0;
}

static bool vm_net_mock_role_service_durability_row(
    void *contextValue, unsigned int columnCount,
    const char *const *values, const size_t *lengths)
{
    vm_net_mock_role_service_load_context *context =
        (vm_net_mock_role_service_load_context *)contextValue;
    u32 slot = 0;
    u32 itemId = 0;
    u32 durability = 0;
    u32 durabilityMax = 0;

    if (context == NULL || context->state == NULL || columnCount != 4 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &slot) ||
        slot >= VM_NET_MOCK_EQUIP_SLOT_COUNT ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &itemId) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &durability) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &durabilityMax) ||
        durabilityMax == 0 || durabilityMax > 0xffffu ||
        durability > durabilityMax)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->state->equipmentItemIds[slot] = itemId;
    context->state->durability[slot] = (u16)durability;
    context->state->durabilityMax[slot] = (u16)durabilityMax;
    return true;
}

static bool vm_net_mock_role_service_skill_row(
    void *contextValue, unsigned int columnCount,
    const char *const *values, const size_t *lengths)
{
    vm_net_mock_role_service_load_context *context =
        (vm_net_mock_role_service_load_context *)contextValue;
    u32 skillId = 0;

    if (context == NULL || context->state == NULL || columnCount != 1 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &skillId) ||
        skillId == 0 ||
        context->state->learnedSkillCount >= VM_NET_MOCK_LEARNED_SKILL_MAX_ITEMS)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->state->learnedSkillIds[context->state->learnedSkillCount++] = skillId;
    return true;
}

static bool vm_net_mock_role_service_persist_durability(
    const vm_net_mock_role_service_state *state, u32 slot)
{
    char accountHex[129];
    char query[768];

    if (state == NULL || slot >= VM_NET_MOCK_EQUIP_SLOT_COUNT ||
        !vm_net_mock_role_service_tables_ensure() ||
        !vm_net_mock_role_service_account_hex(state->accountId, accountHex))
    {
        return false;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO account_role_equipment_durability(account_id,role_id,slot_index,item_id,durability,durability_max) "
             "VALUES(X'%s',%u,%u,%u,%u,%u) ON DUPLICATE KEY UPDATE "
             "item_id=VALUES(item_id),durability=VALUES(durability),durability_max=VALUES(durability_max)",
             accountHex, state->roleId, slot, state->equipmentItemIds[slot],
             state->durability[slot], state->durabilityMax[slot]);
    return vm_mysql_exec(query);
}

static bool vm_net_mock_role_service_persist_skill(
    const vm_net_mock_role_service_state *state, u32 skillId)
{
    char accountHex[129];
    char query[640];

    if (state == NULL || skillId == 0 ||
        !vm_net_mock_role_service_tables_ensure() ||
        !vm_net_mock_role_service_account_hex(state->accountId, accountHex))
    {
        return false;
    }
    snprintf(query, sizeof(query),
             "INSERT IGNORE INTO account_role_skills(account_id,role_id,skill_id,skill_level) "
             "VALUES(X'%s',%u,%u,1)",
             accountHex, state->roleId, skillId);
    return vm_mysql_exec(query);
}

static bool vm_net_mock_role_service_has_skill(
    const vm_net_mock_role_service_state *state, u32 skillId)
{
    if (state == NULL || skillId == 0)
        return false;
    for (u32 i = 0; i < state->learnedSkillCount; ++i)
    {
        if (state->learnedSkillIds[i] == skillId)
            return true;
    }
    return false;
}

static void vm_net_mock_role_service_sync_equipment(
    vm_net_mock_role_service_state *state, const vm_net_mock_role_state *role)
{
    if (state == NULL || role == NULL)
        return;
    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        if (state->equipmentItemIds[slot] == role->equippedItemIds[slot])
            continue;
        state->equipmentItemIds[slot] = role->equippedItemIds[slot];
        state->durabilityMax[slot] = VM_NET_MOCK_EQUIPMENT_DURABILITY_MAX;
        state->durability[slot] = VM_NET_MOCK_EQUIPMENT_DURABILITY_MAX;
        (void)vm_net_mock_role_service_persist_durability(state, slot);
    }
}

static vm_net_mock_role_service_state *vm_net_mock_role_service_state_get(
    const vm_net_mock_role_state *role)
{
    const char *accountId = g_vm_mock_service_active_account_id;
    vm_net_mock_role_service_state *state = NULL;
    vm_net_mock_role_service_load_context context;
    char accountHex[129];
    char query[768];
    u8 rawJob = 0;

    if (role == NULL || accountId == NULL || accountId[0] == 0)
        return NULL;
    for (u32 i = 0; i < VM_NET_MOCK_ROLE_SERVICE_CACHE_MAX; ++i)
    {
        if (g_vm_net_mock_role_service_states[i].used &&
            g_vm_net_mock_role_service_states[i].roleId == role->roleId &&
            strcmp(g_vm_net_mock_role_service_states[i].accountId, accountId) == 0)
        {
            state = &g_vm_net_mock_role_service_states[i];
            vm_net_mock_role_service_sync_equipment(state, role);
            return state;
        }
    }
    for (u32 i = 0; i < VM_NET_MOCK_ROLE_SERVICE_CACHE_MAX; ++i)
    {
        if (!g_vm_net_mock_role_service_states[i].used)
        {
            state = &g_vm_net_mock_role_service_states[i];
            break;
        }
    }
    if (state == NULL)
    {
        state = &g_vm_net_mock_role_service_states[
            g_vm_net_mock_role_service_state_replace_index++ %
            VM_NET_MOCK_ROLE_SERVICE_CACHE_MAX];
    }
    memset(state, 0, sizeof(*state));
    state->used = true;
    state->roleId = role->roleId;
    snprintf(state->accountId, sizeof(state->accountId), "%s", accountId);
    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        state->equipmentItemIds[slot] = role->equippedItemIds[slot];
        state->durability[slot] = VM_NET_MOCK_EQUIPMENT_DURABILITY_MAX;
        state->durabilityMax[slot] = VM_NET_MOCK_EQUIPMENT_DURABILITY_MAX;
    }
    memset(&context, 0, sizeof(context));
    context.state = state;
    if (vm_net_mock_role_service_tables_ensure() &&
        vm_net_mock_role_service_account_hex(accountId, accountHex))
    {
        snprintf(query, sizeof(query),
                 "SELECT slot_index,item_id,durability,durability_max FROM account_role_equipment_durability "
                 "WHERE account_id=X'%s' AND role_id=%u ORDER BY slot_index",
                 accountHex, role->roleId);
        if (!vm_mysql_query(query, vm_net_mock_role_service_durability_row, &context))
            printf("[error][network] mock_role_durability_load role=%u error=%s\n",
                   role->roleId, vm_mysql_last_error());
        snprintf(query, sizeof(query),
                 "SELECT skill_id FROM account_role_skills WHERE account_id=X'%s' AND role_id=%u ORDER BY skill_id",
                 accountHex, role->roleId);
        if (!vm_mysql_query(query, vm_net_mock_role_service_skill_row, &context))
            printf("[error][network] mock_role_skills_load role=%u error=%s\n",
                   role->roleId, vm_mysql_last_error());
    }
    state->loaded = true;
    vm_net_mock_role_service_sync_equipment(state, role);

    /* A role starts with exactly one level-1 profession skill.  Never derive
     * additional learned skills from the role level: every later skill must be
     * persisted by an explicit trainer-NPC learning operation. */
    if (state->learnedSkillCount == 0)
    {
        rawJob = vm_net_mock_role_job_to_skill_raw_job(role->job);
        for (u32 i = 0; i < vm_net_mock_load_skill_catalog(); ++i)
        {
            const vm_net_mock_skill_catalog_item *skill =
                &g_vm_net_mock_skill_catalog[i];
            if (skill->rawJob != rawJob || skill->levelRequired > 1)
                continue;
            state->learnedSkillIds[state->learnedSkillCount++] = skill->skillId;
            (void)vm_net_mock_role_service_persist_skill(state, skill->skillId);
            printf("[info][network] mock_role_skill_seed role=%u job=%u skill=%u policy=starter-only\n",
                   role->roleId, role->job, skill->skillId);
            break;
        }
    }
    printf("[info][network] mock_role_service_load account=%s role=%u skills=%u durability_slots=%u invalid=%u\n",
           accountId, role->roleId, state->learnedSkillCount,
           VM_NET_MOCK_EQUIP_SLOT_COUNT, context.invalid ? 1u : 0u);
    return state;
}

static bool vm_net_mock_role_service_add_skill(vm_net_mock_role_state *role,
                                               u32 skillId)
{
    vm_net_mock_role_service_state *state =
        vm_net_mock_role_service_state_get(role);

    if (state == NULL || skillId == 0 ||
        state->learnedSkillCount >= VM_NET_MOCK_LEARNED_SKILL_MAX_ITEMS ||
        vm_net_mock_role_service_has_skill(state, skillId))
    {
        return false;
    }
    if (!vm_net_mock_role_service_persist_skill(state, skillId))
    {
        printf("[error][network] mock_role_skill_store role=%u skill=%u error=%s\n",
               role ? role->roleId : 0, skillId, vm_mysql_last_error());
        return false;
    }
    state->learnedSkillIds[state->learnedSkillCount++] = skillId;
    return true;
}

static u32 vm_net_mock_role_service_repair_cost(
    vm_net_mock_role_service_state *state,
    const vm_net_mock_role_state *role,
    u16 *repairCountOut)
{
    u32 cost = 0;
    u16 count = 0;

    if (repairCountOut)
        *repairCountOut = 0;
    if (state == NULL || role == NULL)
        return 0;
    vm_net_mock_role_service_sync_equipment(state, role);
    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        if (role->equippedItemIds[slot] == 0 ||
            state->durability[slot] >= state->durabilityMax[slot])
        {
            continue;
        }
        cost += (u32)(state->durabilityMax[slot] - state->durability[slot]);
        ++count;
    }
    if (repairCountOut)
        *repairCountOut = count;
    return cost;
}

static bool vm_net_mock_role_service_repair_all(vm_net_mock_role_state *role,
                                                u16 *repairCountOut,
                                                u32 *costOut)
{
    vm_net_mock_role_service_state *state =
        vm_net_mock_role_service_state_get(role);
    u16 count = 0;
    u32 cost = vm_net_mock_role_service_repair_cost(state, role, &count);

    if (repairCountOut)
        *repairCountOut = count;
    if (costOut)
        *costOut = cost;
    if (state == NULL || role == NULL || role->money < cost)
        return false;
    if (cost == 0)
        return true;
    role->money -= cost;
    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        if (role->equippedItemIds[slot] == 0 ||
            state->durability[slot] >= state->durabilityMax[slot])
        {
            continue;
        }
        state->durability[slot] = state->durabilityMax[slot];
        (void)vm_net_mock_role_service_persist_durability(state, slot);
    }
    vm_net_mock_role_db_save("npc-equipment-repair");
    return true;
}

static void vm_net_mock_role_service_apply_battle_wear(
    vm_net_mock_role_state *role)
{
    vm_net_mock_role_service_state *state =
        vm_net_mock_role_service_state_get(role);

    if (state == NULL || role == NULL || g_mockBattleOperateSessionSerial == 0 ||
        state->lastBattleWearSerial == g_mockBattleOperateSessionSerial)
    {
        return;
    }
    state->lastBattleWearSerial = g_mockBattleOperateSessionSerial;
    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        if (role->equippedItemIds[slot] == 0 || state->durability[slot] == 0)
            continue;
        --state->durability[slot];
        (void)vm_net_mock_role_service_persist_durability(state, slot);
    }
    printf("[info][network] mock_equipment_durability_wear role=%u battle=%u amount=1\n",
           role->roleId, g_mockBattleOperateSessionSerial);
}

static u32 vm_net_mock_build_role_learned_skill_blob(const vm_net_mock_role_state *role,
                                                     u8 *out, u32 outCap,
                                                     u8 *learnedCountOut,
                                                     char *previewOut,
                                                     u32 previewCap)
{
    u32 pos = 0;
    u32 learned = 0;
    vm_net_mock_role_service_state *serviceState =
        vm_net_mock_role_service_state_get(role);
    u8 roleJob = role ? role->job : 1;
    u8 rawJob = vm_net_mock_role_job_to_skill_raw_job(roleJob);
    u32 previewPos = 0;

    if (learnedCountOut)
        *learnedCountOut = 0;
    if (previewOut && previewCap > 0)
        previewOut[0] = 0;
    if (out == NULL || outCap == 0)
        return 0;
    if (serviceState != NULL)
    {
        for (u32 i = 0;
             i < serviceState->learnedSkillCount &&
             learned < VM_NET_MOCK_LEARNED_SKILL_MAX_ITEMS;
             ++i)
        {
            const vm_net_mock_skill_catalog_item *skill =
                vm_net_mock_find_skill_catalog_item(
                    serviceState->learnedSkillIds[i]);
            if (skill == NULL || skill->rawJob != rawJob)
                continue;
            if (!vm_net_mock_seq_put_u32(out, outCap, &pos, skill->skillId))
                break;
            if (previewOut && previewCap > 0)
                vm_net_mock_append_preview_u32(previewOut, previewCap,
                                               &previewPos, skill->skillId);
            ++learned;
        }
    }
    if (learnedCountOut)
        *learnedCountOut = (u8)learned;
    return pos;
}

static bool vm_net_mock_add_shop_catalog_item(u32 itemId, const u8 *name, u32 nameLen,
                                              u32 price, u32 stock, u8 stack, u8 visual,
                                              bool equip, u32 category)
{
    vm_net_mock_shop_catalog_item *item = NULL;
    u32 copyLen = 0;

    if (itemId == 0 || name == NULL || nameLen == 0 ||
        g_vm_net_mock_shop_catalog_count >= VM_NET_MOCK_SHOP_MAX_CATALOG_ITEMS)
    {
        return false;
    }

    item = &g_vm_net_mock_shop_catalog[g_vm_net_mock_shop_catalog_count++];
    memset(item, 0, sizeof(*item));
    item->itemId = itemId;
    copyLen = vm_net_mock_shop_safe_name_len(name, nameLen, VM_NET_MOCK_SHOP_NAME_BYTES);
    memcpy(item->name, name, copyLen);
    item->name[copyLen] = 0;
    item->price = price ? price : VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE;
    item->stock = stock ? stock : VM_NET_MOCK_SHOP_DEFAULT_ITEM_STOCK;
    item->stack = stack ? stack : 1;
    item->visual = visual ? visual : 1;
    item->isEquip = equip ? 1 : 0;
    item->category = (u8)(category > 255 ? 255 : category);
    item->enabled = 1;
    return true;
}

static u32 vm_net_mock_load_shop_catalog_dsh(const char *path, bool equip)
{
    static u8 data[131072];
    u32 len = vm_net_mock_load_response_file(path, data, sizeof(data));
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 pos = 16;
    u32 added = 0;

    if (len < 16)
        return 0;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    if (columnCount == 0 || columnCount > 64 || rowCount > 10000)
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
        u32 itemId = 0;
        u32 price = 0;
        u32 kubaoPrice = 0;
        u32 stock = equip ? 1 : VM_NET_MOCK_SHOP_DEFAULT_ITEM_STOCK;
        u32 visual = 1;
        u32 category = 0xff;
        const u8 *name = NULL;
        u32 nameLen = 0;

        if (rowEnd > len || rowEnd < rowPos)
            break;

        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;
            if (rowPos + valueLen > rowEnd)
                break;

            if (col == 0)
                itemId = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (col == 1)
            {
                name = value;
                nameLen = valueLen;
            }
            else if (!equip && col == 3)
                visual = vm_net_mock_parse_dsh_u32(value, valueLen, 1);
            else if ((!equip && col == 5) || (equip && col == 7))
                category = vm_net_mock_parse_dsh_u32(value, valueLen, 0xff);
            else if ((!equip && col == 8) || (equip && col == 5))
                price = vm_net_mock_parse_dsh_u32(value, valueLen, VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE);
            else if (!equip && col == 29)
                kubaoPrice = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            else if (!equip && col == 10)
                stock = vm_net_mock_parse_dsh_u32(value, valueLen, VM_NET_MOCK_SHOP_DEFAULT_ITEM_STOCK);

            rowPos += valueLen;
        }

        /*
         * item.dsh "价值" matches ordinary item/equipment values, but the mall
         * secret-item page (`类别=14`) uses the dedicated "酷宝" column as the
         * W-coin price. Example rows such as 800/801/806 otherwise appear as
         * 0 or 150000000 in the premium shop.
         */
        if (!equip && category == 14 && kubaoPrice != 0)
            price = kubaoPrice;

        if (vm_net_mock_add_shop_catalog_item(itemId,
                                             name,
                                             nameLen,
                                             price,
                                             stock,
                                             (u8)(stock > 255 ? 255 : stock),
                                             (u8)(visual > 255 ? 1 : visual),
                                             equip,
                                             category))
        {
            ++added;
        }
        pos = rowEnd;
    }

    return added;
}

static u32 vm_net_mock_load_shop_catalog(void)
{
    u32 itemCount = 0;
    u32 equipCount = 0;

    if (g_vm_net_mock_shop_catalog_loaded)
        return g_vm_net_mock_shop_catalog_count;

    g_vm_net_mock_shop_catalog_loaded = true;
    g_vm_net_mock_shop_catalog_count = 0;

    itemCount = vm_net_mock_load_shop_catalog_dsh("JHOnlineData/item.dsh", false);
    if (itemCount == 0)
        itemCount = vm_net_mock_load_shop_catalog_dsh("bin/JHOnlineData/item.dsh", false);
    equipCount = vm_net_mock_load_shop_catalog_dsh("JHOnlineData/equip.dsh", true);
    if (equipCount == 0)
        equipCount = vm_net_mock_load_shop_catalog_dsh("bin/JHOnlineData/equip.dsh", true);

    if (g_vm_net_mock_shop_catalog_count == 0)
    {
        static const char fallbackName[] = "Teleport Stone";
        printf("[warn][network] mock_shop_catalog fallback=item.dsh/equip.dsh-not-found item=%u\n",
               VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_ID);
        (void)vm_net_mock_add_shop_catalog_item(VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_ID,
                                                (const u8 *)fallbackName,
                                                (u32)strlen(fallbackName),
                                                VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE,
                                                VM_NET_MOCK_SHOP_DEFAULT_ITEM_STOCK,
                                                VM_NET_MOCK_BACKPACK_DEFAULT_ITEM_COUNT,
                                                1,
                                                false,
                                                14);
    }
    else
    {
        vm_net_mock_sort_shop_catalog();
        const vm_net_mock_shop_catalog_item *first = &g_vm_net_mock_shop_catalog[0];
        printf("[info][network] mock_shop_catalog total=%u items=%u equips=%u first=%u source=item.dsh/equip.dsh\n",
               g_vm_net_mock_shop_catalog_count, itemCount, equipCount, first->itemId);
    }

    /* Price/availability overrides are authoritative server state.  Failure to
     * load them must not hide the immutable DSH catalog, so retain base values
     * and surface the database error through the admin log. */
    (void)vm_net_mock_shop_admin_db_load();

    vm_autotest_note("mock_shop_catalog_loaded total=%u items=%u equips=%u source=item.dsh/equip.dsh\n",
                     g_vm_net_mock_shop_catalog_count, itemCount, equipCount);
    return g_vm_net_mock_shop_catalog_count;
}

static const vm_net_mock_shop_catalog_item *vm_net_mock_find_shop_catalog_item(u32 itemId)
{
    u32 total = vm_net_mock_load_shop_catalog();

    for (u32 i = 0; i < total; ++i)
    {
        if (g_vm_net_mock_shop_catalog[i].itemId == itemId)
            return &g_vm_net_mock_shop_catalog[i];
    }
    return NULL;
}

typedef struct
{
    u32 loaded;
    u32 skipped;
} vm_net_mock_shop_admin_load_context;

static bool vm_net_mock_shop_admin_db_row(void *contextValue,
                                          unsigned int columnCount,
                                          const char *const *values,
                                          const size_t *lengths)
{
    vm_net_mock_shop_admin_load_context *context =
        (vm_net_mock_shop_admin_load_context *)contextValue;
    u32 itemId = 0;
    u32 price = 0;
    u32 enabled = 0;
    vm_net_mock_shop_catalog_item *item = NULL;

    if (context == NULL || columnCount != 3 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &itemId) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &price) || price == 0 ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &enabled) || enabled > 1)
    {
        if (context != NULL)
            ++context->skipped;
        return true;
    }
    for (u32 i = 0; i < g_vm_net_mock_shop_catalog_count; ++i)
    {
        if (g_vm_net_mock_shop_catalog[i].itemId == itemId)
        {
            item = &g_vm_net_mock_shop_catalog[i];
            break;
        }
    }
    if (item == NULL)
    {
        ++context->skipped;
        return true;
    }
    item->price = price;
    item->enabled = enabled ? 1 : 0;
    ++context->loaded;
    return true;
}

static bool vm_net_mock_shop_admin_db_load(void)
{
    vm_net_mock_shop_admin_load_context context;

    if (g_vm_net_mock_shop_admin_db_loaded)
        return g_vm_net_mock_shop_admin_db_valid;
    g_vm_net_mock_shop_admin_db_loaded = true;
    g_vm_net_mock_shop_admin_db_valid = false;
    memset(&context, 0, sizeof(context));

    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_shop_items ("
            "item_id INT UNSIGNED NOT NULL,price INT UNSIGNED NOT NULL,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(item_id)) ENGINE=InnoDB") ||
        !vm_mysql_query(
            "SELECT item_id,price,enabled FROM server_shop_items ORDER BY item_id",
            vm_net_mock_shop_admin_db_row, &context))
    {
        printf("[error][mock-admin] shop_item_db_load failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_shop_admin_db_valid = true;
    printf("[info][mock-admin] shop_item_db_load rows=%u skipped=%u\n",
           context.loaded, context.skipped);
    return true;
}

static bool vm_net_mock_shop_admin_save(u32 itemId, u32 price, bool enabled,
                                        const char **errorOut)
{
    vm_net_mock_shop_catalog_item *item = NULL;
    char query[512];

    if (errorOut)
        *errorOut = "商品参数无效";
    (void)vm_net_mock_load_shop_catalog();
    if (!g_vm_net_mock_shop_admin_db_valid)
    {
        g_vm_net_mock_shop_admin_db_loaded = false;
        if (!vm_net_mock_shop_admin_db_load())
        {
            if (errorOut)
                *errorOut = vm_mysql_last_error();
            return false;
        }
    }
    if (itemId == 0 || price == 0)
        return false;
    for (u32 i = 0; i < g_vm_net_mock_shop_catalog_count; ++i)
    {
        if (g_vm_net_mock_shop_catalog[i].itemId == itemId)
        {
            item = &g_vm_net_mock_shop_catalog[i];
            break;
        }
    }
    if (item == NULL)
    {
        if (errorOut)
            *errorOut = "商品目录中不存在该物品";
        return false;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_shop_items(item_id,price,enabled) VALUES(%u,%u,%u) "
             "ON DUPLICATE KEY UPDATE price=VALUES(price),enabled=VALUES(enabled)",
             itemId, price, enabled ? 1u : 0u);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    item->price = price;
    item->enabled = enabled ? 1 : 0;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] shop_item_save item=%u price=%u enabled=%u\n",
           itemId, price, enabled ? 1u : 0u);
    return true;
}

static u8 vm_net_mock_equipment_slot_for_category(u32 category)
{
    switch (category)
    {
    case 7: /* sword */
    case 8: /* dagger */
    case 9: /* staff */
        return 0;
    case 0:
        return 1; /* helmet */
    case 1:
        return 2; /* chest */
    case 2:
        return 3; /* cloak */
    case 3:
        return 4; /* belt */
    case 4:
        return 5; /* leggings */
    case 5:
        return 6; /* boots */
    case 6:
        return 7; /* ring */
    default:
        return 0xff;
    }
}

static bool vm_net_mock_add_equipment_catalog_item(u32 itemId, u32 levelRequired,
                                                   u32 category,
                                                   const vm_net_mock_equipment_bonus *bonus)
{
    vm_net_mock_equipment_catalog_item *item = NULL;
    u8 slot = vm_net_mock_equipment_slot_for_category(category);

    if (itemId == 0 || bonus == NULL || slot >= VM_NET_MOCK_EQUIP_SLOT_COUNT ||
        g_vm_net_mock_equipment_catalog_count >= VM_NET_MOCK_EQUIP_CATALOG_MAX_ITEMS)
    {
        return false;
    }

    item = &g_vm_net_mock_equipment_catalog[g_vm_net_mock_equipment_catalog_count++];
    memset(item, 0, sizeof(*item));
    item->itemId = itemId;
    item->slot = slot;
    item->levelRequired = (u8)(levelRequired > 255 ? 255 : levelRequired);
    item->bonus = *bonus;
    return true;
}

static u32 vm_net_mock_load_equipment_catalog_dsh(const char *path)
{
    static u8 data[131072];
    u32 len = vm_net_mock_load_response_file(path, data, sizeof(data));
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 pos = 16;
    u32 added = 0;

    if (len < 16)
        return 0;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    if (columnCount == 0 || columnCount > 64 || rowCount > 10000)
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
        u32 itemId = 0;
        u32 levelRequired = 1;
        u32 category = 0xffffffffu;
        vm_net_mock_equipment_bonus bonus;

        memset(&bonus, 0, sizeof(bonus));
        if (rowEnd > len || rowEnd < rowPos)
            break;

        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;
            u32 parsed = 0;

            if (rowPos + valueLen > rowEnd)
                break;
            parsed = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            switch (col)
            {
            case 0:
                itemId = parsed;
                break;
            case 3:
                levelRequired = parsed ? parsed : 1;
                break;
            case 7:
                category = parsed;
                break;
            case 8:
                bonus.armor = parsed;
                break;
            case 9:
                bonus.attack = parsed;
                break;
            case 10:
                bonus.hp = parsed;
                break;
            case 11:
                bonus.mp = parsed;
                break;
            case 12:
                bonus.strength = parsed;
                break;
            case 13:
                bonus.agility = parsed;
                break;
            case 14:
                bonus.wisdom = parsed;
                break;
            case 15:
                bonus.crit = parsed;
                break;
            case 16:
                bonus.hit = parsed;
                break;
            case 17:
                bonus.dodge = parsed;
                break;
            case 18:
                bonus.resist = parsed;
                break;
            default:
                break;
            }
            rowPos += valueLen;
        }

        if (vm_net_mock_add_equipment_catalog_item(itemId, levelRequired, category, &bonus))
            ++added;
        pos = rowEnd;
    }

    return added;
}

static u32 vm_net_mock_load_equipment_catalog(void)
{
    u32 equipCount = 0;

    if (g_vm_net_mock_equipment_catalog_loaded)
        return g_vm_net_mock_equipment_catalog_count;

    g_vm_net_mock_equipment_catalog_loaded = true;
    g_vm_net_mock_equipment_catalog_count = 0;
    equipCount = vm_net_mock_load_equipment_catalog_dsh("JHOnlineData/equip.dsh");
    if (equipCount == 0)
        equipCount = vm_net_mock_load_equipment_catalog_dsh("bin/JHOnlineData/equip.dsh");

    if (equipCount == 0)
    {
        printf("[warn][network] mock_equip_catalog fallback=equip.dsh-not-found\n");
    }
    else
    {
        printf("[info][network] mock_equip_catalog total=%u source=equip.dsh\n",
               g_vm_net_mock_equipment_catalog_count);
    }
    return g_vm_net_mock_equipment_catalog_count;
}

static const vm_net_mock_equipment_catalog_item *vm_net_mock_find_equipment_catalog_item(u32 itemId)
{
    u32 total = vm_net_mock_load_equipment_catalog();

    if (itemId == 0)
        return NULL;
    for (u32 i = 0; i < total; ++i)
    {
        if (g_vm_net_mock_equipment_catalog[i].itemId == itemId)
            return &g_vm_net_mock_equipment_catalog[i];
    }
    return NULL;
}

static bool vm_net_mock_add_item_effect_catalog_item(u32 itemId, u32 category,
                                                     u32 levelRequired, u32 stack,
                                                     u32 consumeMode,
                                                     u32 hp, u32 mp, u32 exp)
{
    vm_net_mock_item_effect_catalog_item *item = NULL;

    if (itemId == 0 ||
        g_vm_net_mock_item_effect_catalog_count >= VM_NET_MOCK_ITEM_EFFECT_CATALOG_MAX_ITEMS)
    {
        return false;
    }

    item = &g_vm_net_mock_item_effect_catalog[g_vm_net_mock_item_effect_catalog_count++];
    memset(item, 0, sizeof(*item));
    item->itemId = itemId;
    item->category = (u8)(category > 255 ? 255 : category);
    item->levelRequired = (u8)(levelRequired > 255 ? 255 : levelRequired);
    item->stack = (u8)(stack > 255 ? 255 : stack);
    item->consumeMode = (u8)(consumeMode > 255 ? 255 : consumeMode);
    item->hp = hp;
    item->mp = mp;
    item->exp = exp;
    return true;
}

static u32 vm_net_mock_load_item_effect_catalog_dsh(const char *path)
{
    static u8 data[131072];
    u32 len = vm_net_mock_load_response_file(path, data, sizeof(data));
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 pos = 16;
    u32 added = 0;

    if (len < 16)
        return 0;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    if (columnCount == 0 || columnCount > 64 || rowCount > 10000)
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
        u32 itemId = 0;
        u32 category = 0xff;
        u32 levelRequired = 1;
        u32 stack = 1;
        u32 consumeMode = 0;
        u32 hp = 0;
        u32 mp = 0;
        u32 exp = 0;

        if (rowEnd > len || rowEnd < rowPos)
            break;

        for (u32 col = 0; col < columnCount && rowPos < rowEnd; ++col)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;
            u32 parsed = 0;

            if (rowPos + valueLen > rowEnd)
                break;
            parsed = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
            switch (col)
            {
            case 0:
                itemId = parsed;
                break;
            case 5:
                category = parsed;
                break;
            case 6:
                levelRequired = parsed ? parsed : 1;
                break;
            case 10:
                stack = parsed ? parsed : 1;
                break;
            case 12:
                consumeMode = parsed;
                break;
            case 15:
                hp = parsed;
                break;
            case 16:
                mp = parsed;
                break;
            case 17:
                exp = parsed;
                break;
            default:
                break;
            }
            rowPos += valueLen;
        }

        if (vm_net_mock_add_item_effect_catalog_item(itemId, category, levelRequired,
                                                     stack, consumeMode, hp, mp, exp))
        {
            ++added;
        }
        pos = rowEnd;
    }

    return added;
}

static u32 vm_net_mock_load_item_effect_catalog(void)
{
    u32 itemCount = 0;

    if (g_vm_net_mock_item_effect_catalog_loaded)
        return g_vm_net_mock_item_effect_catalog_count;

    g_vm_net_mock_item_effect_catalog_loaded = true;
    g_vm_net_mock_item_effect_catalog_count = 0;
    itemCount = vm_net_mock_load_item_effect_catalog_dsh("JHOnlineData/item.dsh");
    if (itemCount == 0)
        itemCount = vm_net_mock_load_item_effect_catalog_dsh("bin/JHOnlineData/item.dsh");

    if (itemCount == 0)
    {
        (void)vm_net_mock_add_item_effect_catalog_item(301, 10, 1, 20, 1, 100, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(302, 10, 1, 20, 1, 350, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(303, 10, 1, 20, 1, 600, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(304, 10, 1, 20, 1, 850, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(305, 10, 1, 20, 1, 1100, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(321, 10, 1, 20, 1, 0, 100, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(322, 10, 1, 20, 1, 0, 350, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(323, 10, 1, 20, 1, 0, 600, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(324, 10, 1, 20, 1, 0, 850, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(325, 10, 1, 20, 1, 0, 1100, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(802, 10, 200, 1, 2, 50000, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(803, 10, 200, 1, 2, 0, 50000, 0);
        printf("[warn][network] mock_item_effect_catalog fallback=item.dsh-not-found total=%u\n",
               g_vm_net_mock_item_effect_catalog_count);
    }
    else
    {
        printf("[info][network] mock_item_effect_catalog total=%u source=item.dsh\n",
               g_vm_net_mock_item_effect_catalog_count);
    }
    return g_vm_net_mock_item_effect_catalog_count;
}

static const vm_net_mock_item_effect_catalog_item *vm_net_mock_find_item_effect_catalog_item(u32 itemId)
{
    u32 total = vm_net_mock_load_item_effect_catalog();

    if (itemId == 0)
        return NULL;
    for (u32 i = 0; i < total; ++i)
    {
        if (g_vm_net_mock_item_effect_catalog[i].itemId == itemId)
            return &g_vm_net_mock_item_effect_catalog[i];
    }
    return NULL;
}

static bool vm_net_mock_item_effect_is_usable(const vm_net_mock_item_effect_catalog_item *item)
{
    if (item == NULL)
        return false;
    return item->category == 10 || item->hp != 0 || item->mp != 0 || item->exp != 0;
}

static bool vm_net_mock_item_effect_is_reservoir(
    const vm_net_mock_item_effect_catalog_item *item)
{
    return item != NULL && item->consumeMode == 2 &&
           (item->hp != 0 || item->mp != 0);
}

static u32 vm_net_mock_item_effect_reservoir_capacity(
    const vm_net_mock_item_effect_catalog_item *item)
{
    if (!vm_net_mock_item_effect_is_reservoir(item))
        return 0;
    return item->hp > item->mp ? item->hp : item->mp;
}

static u32 vm_net_mock_item_effect_plan_reservoir_restore(
    const vm_net_mock_item_effect_catalog_item *item,
    u32 remaining, u32 missingHp, u32 missingMp,
    u32 *hpOut, u32 *mpOut)
{
    u32 available = remaining;
    u32 hp = 0;
    u32 mp = 0;

    if (hpOut)
        *hpOut = 0;
    if (mpOut)
        *mpOut = 0;
    if (!vm_net_mock_item_effect_is_reservoir(item) || available == 0)
        return 0;

    hp = vm_net_mock_min_u32(missingHp, vm_net_mock_min_u32(item->hp, available));
    available -= hp;
    mp = vm_net_mock_min_u32(missingMp, vm_net_mock_min_u32(item->mp, available));
    if (hpOut)
        *hpOut = hp;
    if (mpOut)
        *mpOut = mp;
    return hp + mp;
}

static bool vm_net_mock_shop17_should_include_item(
    const vm_net_mock_shop_catalog_item *item);
static u32 vm_net_mock_shop17_order_group(u32 itemId);

static void vm_net_mock_append_preview_u32(char *out, u32 outCap, u32 *pos, u32 value)
{
    int written = 0;
    if (out == NULL || outCap == 0 || pos == NULL || *pos >= outCap)
        return;
    written = snprintf(out + *pos, outCap - *pos, "%s%u", *pos ? "," : "", value);
    if (written < 0)
        return;
    if ((u32)written >= outCap - *pos)
        *pos = outCap - 1;
    else
        *pos += (u32)written;
}

static u32 vm_net_mock_shop_page_item_limit(u8 subtype)
{
    if (subtype == 5)
        return VM_NET_MOCK_SHOP_SECRET_MAX_ITEMS;
    if (subtype >= 6 && subtype <= 13)
        return VM_NET_MOCK_SHOP_EQUIP_CATEGORY_MAX_ITEMS;
    return VM_NET_MOCK_SHOP_MAX_CATALOG_ITEMS;
}

static u32 vm_net_mock_shop_client_backpack_expand_price(void)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u32 capacity = vm_net_mock_env_u8("CBE_ACTOR_BACKPACK_CAPACITY",
                                      role ? role->backpackCapacity :
                                      VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY);

    /*
     * mmShopMstarWqvga.cbm:sub_74E overrides item 806's visible row price by
     * local backpack-capacity tier instead of trusting the raw server value.
     */
    if (capacity == 12)
        return 20;
    if (capacity == 16)
        return 40;
    return 60;
}

static u32 vm_net_mock_shop_effective_unit_price(u32 itemId, u32 catalogPrice)
{
    if (itemId == VM_NET_MOCK_BACKPACK_EXPAND_ITEM_ID)
        return vm_net_mock_shop_client_backpack_expand_price();
    if (catalogPrice == 0)
        return VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE;
    return catalogPrice;
}

static u8 vm_net_mock_shop_page_equipment_slot(const vm_net_mock_shop_catalog_item *item)
{
    if (item == NULL || !item->isEquip)
        return 0xff;
    return vm_net_mock_equipment_slot_for_category(item->category);
}

static bool vm_net_mock_shop_page_item_matches_subtype(u8 subtype,
                                                       const vm_net_mock_shop_catalog_item *item)
{
    u8 slot = vm_net_mock_shop_page_equipment_slot(item);

    if (item == NULL || !item->enabled)
        return false;
    switch (subtype)
    {
    case 5:  /* 秘宝道具 */
        return !item->isEquip && item->category == 14;
    case 6:  /* 神兵利器 -> 武器 */
        return slot == 0;
    case 7:  /* 衣服 */
        return slot == 2;
    case 8:  /* 裤子 */
        return slot == 5;
    case 9:  /* 帽子 */
        return slot == 1;
    case 10: /* 鞋子 */
        return slot == 6;
    case 11: /* 束腰 */
        return slot == 4;
    case 12: /* 披风 */
        return slot == 3;
    case 13: /* 饰品 */
        return slot == 7;
    default:
        return false;
    }
}

static u32 vm_net_mock_shop_page_filtered_total(u8 subtype)
{
    u32 total = vm_net_mock_load_shop_catalog();
    u32 limit = vm_net_mock_shop_page_item_limit(subtype);
    u32 count = 0;

    for (u32 i = 0; i < total && count < limit; ++i)
    {
        if (vm_net_mock_shop_page_item_matches_subtype(subtype, &g_vm_net_mock_shop_catalog[i]))
            ++count;
    }
    return count;
}

static const vm_net_mock_shop_catalog_item *vm_net_mock_shop_page_item_at(u8 subtype, u32 ordinal)
{
    u32 total = vm_net_mock_load_shop_catalog();
    u32 limit = vm_net_mock_shop_page_item_limit(subtype);
    u32 seen = 0;

    if (ordinal >= limit)
        return NULL;
    for (u32 i = 0; i < total && seen < limit; ++i)
    {
        const vm_net_mock_shop_catalog_item *item = &g_vm_net_mock_shop_catalog[i];
        if (!vm_net_mock_shop_page_item_matches_subtype(subtype, item))
            continue;
        if (seen == ordinal)
            return item;
        ++seen;
    }
    return NULL;
}

static const char *vm_net_mock_shop_page_subtype_name(u8 subtype)
{
    switch (subtype)
    {
    case 5:
        return "secret";
    case 6:
        return "weapon";
    case 7:
        return "chest";
    case 8:
        return "leggings";
    case 9:
        return "helmet";
    case 10:
        return "boots";
    case 11:
        return "belt";
    case 12:
        return "cloak";
    case 13:
        return "accessory";
    default:
        return "unknown";
    }
}

static void vm_net_mock_format_shop_page_ids(u8 subtype, u32 pageIndex, u32 maxRows,
                                             char *out, u32 outCap)
{
    u32 pos = 0;
    u32 total = vm_net_mock_shop_page_filtered_total(subtype);
    u32 start = pageIndex * VM_NET_MOCK_SHOP_PAGE_SIZE;
    u32 rowCount = 0;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (start >= total)
        return;
    rowCount = total - start;
    if (rowCount > VM_NET_MOCK_SHOP_PAGE_SIZE)
        rowCount = VM_NET_MOCK_SHOP_PAGE_SIZE;
    if (rowCount > maxRows)
        rowCount = maxRows;
    for (u32 i = 0; i < rowCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            vm_net_mock_shop_page_item_at(subtype, start + i);
        if (item == NULL)
            break;
        vm_net_mock_append_preview_u32(out, outCap, &pos, item->itemId);
    }
}

static void vm_net_mock_format_shop17_ids(u32 maxRows, char *out, u32 outCap)
{
    u32 pos = 0;
    u32 total = vm_net_mock_load_shop_catalog();
    u32 filteredCount = 0;
    u32 availableCount = 0;
    u32 rowCount = 0;
    u32 emitted = 0;
    bool useFilteredCatalog = false;

    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    for (u32 i = 0; i < total; ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            &g_vm_net_mock_shop_catalog[i];
        if (!item->enabled)
            continue;
        ++availableCount;
        if (vm_net_mock_shop17_should_include_item(item))
            ++filteredCount;
    }
    useFilteredCatalog = filteredCount > 0;
    rowCount = useFilteredCatalog ? filteredCount : availableCount;
    if (rowCount > VM_NET_MOCK_SHOP17_MAX_CATALOG_ITEMS)
        rowCount = VM_NET_MOCK_SHOP17_MAX_CATALOG_ITEMS;
    if (rowCount > maxRows)
        rowCount = maxRows;

    for (u32 group = 0; group < 3 && emitted < rowCount; ++group)
    {
        for (u32 i = 0; i < total && emitted < rowCount; ++i)
        {
            const vm_net_mock_shop_catalog_item *item = &g_vm_net_mock_shop_catalog[i];
            if (!item->enabled ||
                (useFilteredCatalog && !vm_net_mock_shop17_should_include_item(item)))
                continue;
            if (vm_net_mock_shop17_order_group(item->itemId) != group)
                continue;
            vm_net_mock_append_preview_u32(out, outCap, &pos, item->itemId);
            ++emitted;
        }
    }
}

static bool vm_net_mock_seq_put_item_common_extra(u8 *out, u32 outCap,
                                                   u32 *pos,
                                                   u8 stackRuntimeByte,
                                                   u8 enhanceLevel)
{
    /*
     * JianghuOL.CBE:ParseEquipAttributes (vtable +2452) reads two i16-ish
     * fields, then one u8 attr-count. It only consumes attr slots when that
     * count is nonzero, so zero-attr rows must stop after the count byte.
     */
    if (!vm_net_mock_seq_put_i16(out, outCap, pos, stackRuntimeByte))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, pos, enhanceLevel))
        return false;
    return vm_net_mock_seq_put_u8(out, outCap, pos, 0);
}

static bool vm_net_mock_seq_put_shop_page_item_extra(u8 *out, u32 outCap, u32 *pos, u8 stackRuntimeByte)
{
    /*
     * mmShopMstarWqvga.cbm:sub_7BC calls a shop-page item-extra reader after
     * itemId/name/visual/stack/price/stock/flag. The reader is the same
     * ParseEquipAttributes helper as mmGame:0x418C; the six attr arrays are
     * destination capacity, not fields to send when attr-count is zero.
     */
    if (!vm_net_mock_seq_put_i16(out, outCap, pos, stackRuntimeByte))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, pos, 0))
        return false;
    return vm_net_mock_seq_put_u8(out, outCap, pos, 0);
}

static bool vm_net_mock_build_backpack_iteminfo_blob(u8 *out, u32 outCap,
                                                     const vm_net_mock_role_state *role,
                                                     u32 *blobLenOut, u32 *rowCountOut)
{
    u32 pos = 0;
    u8 itemCount = vm_net_mock_role_backpack_count(role);
    if (out == NULL || blobLenOut == NULL)
        return false;
    if (rowCountOut)
        *rowCountOut = 0;

    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, itemCount))
        return false;
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, item->itemId))
            return false;
        if (!vm_net_mock_seq_put_item_common_extra(
                out, outCap, &pos, vm_net_mock_backpack_stack_byte(item),
                (u8)SDL_min(item->enhanceLevel,
                            VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)))
        {
            return false;
        }
    }
    *blobLenOut = pos;
    if (rowCountOut)
        *rowCountOut = itemCount;
    return true;
}

static bool vm_net_mock_shop17_should_include_item(
    const vm_net_mock_shop_catalog_item *item)
{
    /*
     * The 17/1 list is rendered by mmGame:0x418C.  The NPC purchase screen in
     * this path is an equipment shop, so prefer equip.dsh ids (>=1000) and omit
     * low material/task drops.  Keep the packet page-sized: the parser copies
     * iteminfo into a 1024-byte stream buffer.
     */
    return item != NULL && item->enabled && item->itemId >= 800;
}

static u32 vm_net_mock_shop17_order_group(u32 itemId)
{
    if (itemId >= 1000)
        return 0;
    if (itemId >= 800)
        return 1;
    return 2;
}

static u32 vm_net_mock_shop17_first_item_id(void)
{
    u32 total = vm_net_mock_load_shop_catalog();
    for (u32 group = 0; group < 3; ++group)
    {
        for (u32 i = 0; i < total; ++i)
        {
            const vm_net_mock_shop_catalog_item *item =
                &g_vm_net_mock_shop_catalog[i];
            if (vm_net_mock_shop17_should_include_item(item) &&
                vm_net_mock_shop17_order_group(item->itemId) == group)
            {
                return item->itemId;
            }
        }
    }
    for (u32 i = 0; i < total; ++i)
    {
        if (g_vm_net_mock_shop_catalog[i].enabled)
            return g_vm_net_mock_shop_catalog[i].itemId;
    }
    return 0;
}

static bool vm_net_mock_build_shop17_iteminfo_blob(u8 *out, u32 outCap,
                                                   u32 *blobLenOut, u32 *rowCountOut)
{
    u32 pos = 0;
    u32 total = vm_net_mock_load_shop_catalog();
    u32 filteredCount = 0;
    u32 availableCount = 0;
    u32 rowCount = 0;
    bool useFilteredCatalog = false;

    if (out == NULL || blobLenOut == NULL)
        return false;
    if (rowCountOut)
        *rowCountOut = 0;

    for (u32 i = 0; i < total; ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            &g_vm_net_mock_shop_catalog[i];
        if (!item->enabled)
            continue;
        ++availableCount;
        if (vm_net_mock_shop17_should_include_item(item))
            ++filteredCount;
    }
    useFilteredCatalog = filteredCount > 0;
    rowCount = useFilteredCatalog ? filteredCount : availableCount;
    if (rowCount > VM_NET_MOCK_SHOP17_MAX_CATALOG_ITEMS)
        rowCount = VM_NET_MOCK_SHOP17_MAX_CATALOG_ITEMS;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, (u8)rowCount))
        return false;

    for (u32 group = 0, emitted = 0; group < 3 && emitted < rowCount; ++group)
    {
        for (u32 i = 0; i < total && emitted < rowCount; ++i)
        {
            const vm_net_mock_shop_catalog_item *item = &g_vm_net_mock_shop_catalog[i];
            if (!item->enabled ||
                (useFilteredCatalog && !vm_net_mock_shop17_should_include_item(item)))
                continue;
            if (vm_net_mock_shop17_order_group(item->itemId) != group)
                continue;
            if (!vm_net_mock_seq_put_u32(out, outCap, &pos, item->itemId))
                return false;
            if (!vm_net_mock_seq_put_item_common_extra(out, outCap, &pos,
                                                       item->stack, 0))
                return false;
            ++emitted;
        }
    }

    *blobLenOut = pos;
    if (rowCountOut)
        *rowCountOut = rowCount;
    return true;
}

static bool vm_net_mock_build_backpack_grid_iteminfo_blob(u8 *out, u32 outCap,
                                                         const vm_net_mock_role_state *role,
                                                         u32 *blobLenOut, u32 *gridCountOut)
{
    u32 pos = 0;
    u8 itemCount = vm_net_mock_role_backpack_count(role);
    if (out == NULL || blobLenOut == NULL)
        return false;
    if (gridCountOut)
        *gridCountOut = 0;

    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, item->itemId))
            return false;
        if (!vm_net_mock_seq_put_i16(out, outCap, &pos, item->seq))
            return false;
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos,
                                     vm_net_mock_backpack_grid_wire_count(item)))
            return false;
        if (!vm_net_mock_seq_put_item_common_extra(
                out, outCap, &pos, 0,
                (u8)SDL_min(item->enhanceLevel,
                            VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)))
            return false;
    }
    *blobLenOut = pos;
    if (gridCountOut)
        *gridCountOut = itemCount;
    return true;
}

static bool vm_net_mock_build_shop_iteminfo_page_blob(u8 *out, u32 outCap, u32 *blobLenOut,
                                                      u8 subtype, u32 pageIndex,
                                                      u32 *rowCountOut)
{
    u32 pos = 0;
    u32 total = vm_net_mock_shop_page_filtered_total(subtype);
    u32 start = pageIndex * VM_NET_MOCK_SHOP_PAGE_SIZE;
    u32 rowCount = 0;
    if (out == NULL || blobLenOut == NULL)
        return false;
    if (rowCountOut)
        *rowCountOut = 0;

    /*
     * mmShopMstarWqvga.cbm:sub_7BC reads:
     *   u8 row_count,
     *   u32 itemId, string itemName, u8 visual/status, u8 stackOrLimit,
     *   u32 price, u32 stock, u8 flag, then the common item-extra block.
     */
    if (start < total)
    {
        rowCount = total - start;
        if (rowCount > VM_NET_MOCK_SHOP_PAGE_SIZE)
            rowCount = VM_NET_MOCK_SHOP_PAGE_SIZE;
    }

    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, (u8)rowCount))
        return false;
    for (u32 i = 0; i < rowCount; ++i)
    {
        const vm_net_mock_shop_catalog_item *item =
            vm_net_mock_shop_page_item_at(subtype, start + i);
        u32 unitPrice = 0;
        if (item == NULL)
            return false;
        unitPrice = vm_net_mock_shop_effective_unit_price(item->itemId, item->price);
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, item->itemId))
            return false;
        if (!vm_net_mock_seq_put_string(out, outCap, &pos, item->name))
            return false;
        if (!vm_net_mock_seq_put_u8(out, outCap, &pos, item->visual))
            return false;
        if (!vm_net_mock_seq_put_u8(out, outCap, &pos, item->stack))
            return false;
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, unitPrice))
            return false;
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, item->stock))
            return false;
        /*
         * sub_2EB6 copies row+16..row+63 into the call stack, and sub_2E88
         * reads row+60 back as the initial purchase count/step. A zero here
         * makes the W-coin purchase dialog divide by zero while formatting
         * "花费%dW币".
         */
        if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
            return false;
        if (!vm_net_mock_seq_put_shop_page_item_extra(out, outCap, &pos, item->stack))
            return false;
    }

    *blobLenOut = pos;
    if (rowCountOut)
        *rowCountOut = rowCount;
    return true;
}

static bool vm_net_mock_append_shop_catalog_page_object(u8 *out, u32 outCap, u32 *pos,
                                                        u8 subtype, u32 pageIndex,
                                                        u32 *totalOut, u32 *rowCountOut,
                                                        u32 *itemInfoLenOut)
{
    u32 objectStart = 0;
    u8 itemInfo[4096];
    u32 itemInfoLen = 0;
    u32 rowCount = 0;
    u32 total = vm_net_mock_shop_page_filtered_total(subtype);

    memset(itemInfo, 0, sizeof(itemInfo));
    if (!vm_net_mock_build_shop_iteminfo_page_blob(itemInfo, sizeof(itemInfo),
                                                  &itemInfoLen, subtype,
                                                  pageIndex, &rowCount))
    {
        return false;
    }
    if (itemInfoLen > 0xffff)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 14, subtype, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "totalnum", total))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", itemInfo, (u16)itemInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    if (totalOut)
        *totalOut = total;
    if (rowCountOut)
        *rowCountOut = rowCount;
    if (itemInfoLenOut)
        *itemInfoLenOut = itemInfoLen;
    return true;
}

static bool vm_net_mock_append_shop_open_status14_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 14, 14, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_string(out, outCap, pos, "shopinfo", "Shop"))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_shop_wcoin_balance(void)
{
    return vm_net_mock_role_wcoin_balance(vm_net_mock_active_role());
}

static bool vm_net_mock_append_shop_money4_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u32 money = vm_net_mock_shop_wcoin_balance();
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 14, 4, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "coolmoney", money))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "ticket", 0))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static bool vm_net_mock_append_shop_actor_state14_object(u8 *out, u32 outCap, u32 *pos,
                                                         u32 *actorInfoLenOut)
{
    u32 objectStart = 0;
    u8 actorInfo[512];
    u32 actorInfoLen = 0;

    if (actorInfoLenOut)
        *actorInfoLenOut = 0;

    memset(actorInfo, 0, sizeof(actorInfo));
    actorInfoLen = vm_net_mock_build_actor_info(actorInfo, sizeof(actorInfo));
    if (actorInfoLen == 0 || actorInfoLen > 0xffff)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 1, 14, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "revivetype", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "ruffianflag", 0))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "type", 0))
        return false;
    if (!vm_net_mock_put_object_entry(out, outCap, pos, "actorinfo", actorInfo, (u16)actorInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    if (actorInfoLenOut)
        *actorInfoLenOut = actorInfoLen;
    return true;
}

static bool vm_net_mock_append_shop_empty_page14_object(u8 *out, u32 outCap, u32 *pos, u8 subtype)
{
    u32 objectStart = 0;
    const u8 emptyItemInfo[] = {0};
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 14, subtype, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u32(out, outCap, pos, "totalnum", 0))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo",
                                    emptyItemInfo, (u16)sizeof(emptyItemInfo)))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

static u32 vm_net_mock_get_shop_page_index_in_request(const u8 *request, u32 requestLen,
                                                      u8 subtype, u32 fallback)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return fallback;
    while (vm_net_mock_next_request_object(request, requestLen, &offset, &object))
    {
        u32 index = 0;
        u8 index8 = 0;
        if (object.major == 1 && object.kind == 14 && object.subtype == subtype)
        {
            if (vm_net_mock_get_object_u32_field(object.payload, object.payloadLen, "index", &index))
                return index;
            if (vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "index", &index8))
                return index8;
            return fallback;
        }
    }
    return fallback;
}

static bool vm_net_mock_is_backpack_items_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen &&
           object.major == 1 &&
           object.kind == 17 &&
           object.subtype == 1 &&
           object.payloadLen == 0;
}

static bool vm_net_mock_is_backpack_open_request(const u8 *request, u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    return offset == requestLen &&
           object.major == 1 &&
           object.kind == 7 &&
           object.subtype == 42 &&
           object.payloadLen == 0;
}

static bool vm_net_mock_is_backpack_items_books_combo_request(const u8 *request, u32 requestLen,
                                                              u16 *itemsPayloadLenOut)
{
    u32 offset = 4;
    vm_net_mock_request_object itemsObject;
    vm_net_mock_request_object booksObject;

    if (itemsPayloadLenOut)
        *itemsPayloadLenOut = 0;
    if (request == NULL || requestLen < 14 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &itemsObject))
        return false;
    if (itemsObject.major != 1 ||
        itemsObject.kind != 17 ||
        itemsObject.subtype != 1)
    {
        return false;
    }
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &booksObject))
        return false;
    if (booksObject.major != 1 ||
        booksObject.kind != 7 ||
        booksObject.subtype != 42 ||
        booksObject.payloadLen != 0)
    {
        return false;
    }
    if (offset != requestLen)
        return false;

    if (itemsPayloadLenOut)
        *itemsPayloadLenOut = itemsObject.payloadLen;
    return true;
}

typedef struct
{
    u32 itemId;
    u16 seq;
    u32 count;
    u32 num;
    u32 hp;
    u32 mp;
    u32 exp;
    u8 type;
    bool haveItemSelector;
    bool haveEffect;
} vm_net_mock_item_use_request;

typedef struct
{
    u32 itemId;
    u16 seq;
    u32 count;
    u8 type;
    bool haveItemSelector;
} vm_net_mock_item_discard_request;

typedef struct
{
    u32 itemId;
    u16 seq;
    u8 type;
    bool haveItemSelector;
} vm_net_mock_item_equip_request;

/*
 * Replacing an item in an occupied equipment slot is not the 7/8 type=3
 * operation.  JianghuOL.CBE:0x010328D4 serializes its request as
 * 1/7/9 { body:u16, bag:u16 }, where body is the equipped row sequence and
 * bag is the selected backpack row sequence.  The caller appends 1/2/10 as
 * part of the same WT packet.
 */
typedef struct
{
    u16 bodySeq;
    u16 backpackSeq;
    bool hasActorOtherCompanion;
} vm_net_mock_item_equip_swap_request;

typedef struct
{
    u8 subtype;
    u16 equipSeq;
    const u8 *occultInfo;
    u16 occultInfoLen;
    u8 materialRows;
} vm_net_mock_equipment_enhance_request;

typedef struct
{
    u32 index;
    u16 seq;
} vm_net_mock_battle_item_use_request;

static bool vm_net_mock_get_object_number_field(const u8 *payload, u32 payloadLen,
                                                const char *field, u32 *value)
{
    u32 value32 = 0;
    u16 value16 = 0;
    u8 value8 = 0;

    if (value)
        *value = 0;
    if (vm_net_mock_get_object_u32_field(payload, payloadLen, field, &value32))
    {
        if (value)
            *value = value32;
        return true;
    }
    if (vm_net_mock_get_object_u16_field(payload, payloadLen, field, &value16))
    {
        if (value)
            *value = value16;
        return true;
    }
    if (vm_net_mock_get_object_u8_field(payload, payloadLen, field, &value8))
    {
        if (value)
            *value = value8;
        return true;
    }
    return false;
}

static bool vm_net_mock_item_id_is_active_backpack_row(u32 itemId)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    return vm_net_mock_role_find_backpack_item(role, itemId, 0) != NULL;
}

static bool vm_net_mock_parse_item_use_request(const u8 *request, u32 requestLen,
                                               vm_net_mock_item_use_request *parsedOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_item_use_request parsed;
    u32 candidate = 0;
    u32 value = 0;
    bool haveCandidate = false;

    if (parsedOut)
        memset(parsedOut, 0, sizeof(*parsedOut));
    memset(&parsed, 0, sizeof(parsed));
    parsed.count = 1;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (offset != requestLen)
        return false;
    if (object.major != 1 || object.kind != 7 || object.subtype != 1 || object.payloadLen == 0)
        return false;

    (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &parsed.type);
    if (!vm_net_mock_get_object_u16_field(object.payload, object.payloadLen, "seq", &parsed.seq))
    {
        if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "seq", &value) &&
            value <= 0xffffu)
        {
            parsed.seq = (u16)value;
        }
    }

    haveCandidate = vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemId", &candidate) ||
                    vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemID", &candidate) ||
                    vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemid", &candidate);
    if (haveCandidate)
    {
        parsed.itemId = candidate;
        parsed.haveItemSelector = true;
    }

    if (!parsed.haveItemSelector &&
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "id", &candidate))
    {
        const vm_net_mock_item_effect_catalog_item *effect =
            vm_net_mock_find_item_effect_catalog_item(candidate);
        if (vm_net_mock_item_effect_is_usable(effect) ||
            vm_net_mock_item_id_is_active_backpack_row(candidate))
        {
            parsed.itemId = candidate;
            parsed.haveItemSelector = true;
        }
    }

    if (parsed.seq != 0)
        parsed.haveItemSelector = true;

    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "num", &parsed.num);
    if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "count", &value) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "usecount", &value) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "useCount", &value))
    {
        parsed.count = value ? value : 1;
    }
    if (parsed.count == 0)
        parsed.count = 1;
    if (parsed.count > 99)
        parsed.count = 99;

    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "hp", &parsed.hp);
    if (parsed.hp == 0)
        (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "HP", &parsed.hp);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "mp", &parsed.mp);
    if (parsed.mp == 0)
        (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "MP", &parsed.mp);
    (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "exp", &parsed.exp);
    if (parsed.exp == 0)
        (void)vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "EXP", &parsed.exp);
    if (parsed.hp == 0 && parsed.mp == 0 && parsed.exp == 0 && parsed.num != 0)
    {
        if (parsed.type == 2)
            parsed.mp = parsed.num;
        else if (parsed.type == 3)
            parsed.exp = parsed.num;
        else
            parsed.hp = parsed.num;
    }
    parsed.haveEffect = parsed.hp != 0 || parsed.mp != 0 || parsed.exp != 0;

    if (!parsed.haveItemSelector && !parsed.haveEffect)
        return false;

    if (parsedOut)
        *parsedOut = parsed;
    return true;
}

static bool vm_net_mock_parse_item_discard_request(const u8 *request, u32 requestLen,
                                                   vm_net_mock_item_discard_request *parsedOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_item_discard_request parsed;
    u32 value = 0;
    u32 candidate = 0;

    if (parsedOut)
        memset(parsedOut, 0, sizeof(*parsedOut));
    memset(&parsed, 0, sizeof(parsed));

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
        return false;
    if (offset != requestLen)
        return false;
    if (object.major != 1 || object.kind != 7 || object.subtype != 4 || object.payloadLen == 0)
        return false;

    (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &parsed.type);
    if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "seq", &value) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemseq", &value) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemSeq", &value))
    {
        if (value <= 0xffffu)
        {
            parsed.seq = (u16)value;
            parsed.haveItemSelector = true;
        }
    }

    if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemId", &candidate) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemID", &candidate) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemid", &candidate) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "id", &candidate))
    {
        parsed.itemId = candidate;
        parsed.haveItemSelector = true;
    }

    if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "count", &value) ||
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "num", &value))
    {
        parsed.count = value;
    }

    if (!parsed.haveItemSelector)
        return false;
    if (parsedOut)
        *parsedOut = parsed;
    return true;
}

static bool vm_net_mock_parse_item_equip_request(const u8 *request, u32 requestLen,
                                                 vm_net_mock_item_equip_request *parsedOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    vm_net_mock_item_equip_request parsed;
    u32 value = 0;
    u32 candidate = 0;
    u8 kind = 0;
    u8 subtype = 0;

    if (parsedOut)
        memset(parsedOut, 0, sizeof(*parsedOut));
    memset(&parsed, 0, sizeof(parsed));

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype) ||
        kind != 7 || subtype != 8)
        return false;

    if (!vm_net_mock_get_object_u8_field(request, requestLen, "type", &parsed.type))
    {
        if (vm_net_mock_get_object_number_field(request, requestLen, "type", &value) &&
            value <= 0xffu)
        {
            parsed.type = (u8)value;
        }
    }
    if (parsed.type != 3 && parsed.type != 4)
        return false;

    if (vm_net_mock_get_object_number_field(request, requestLen, "seq", &value) ||
        vm_net_mock_get_object_number_field(request, requestLen, "itemseq", &value) ||
        vm_net_mock_get_object_number_field(request, requestLen, "itemSeq", &value))
    {
        if (value <= 0xffffu)
        {
            parsed.seq = (u16)value;
            parsed.haveItemSelector = true;
        }
    }

    if (vm_net_mock_get_object_number_field(request, requestLen, "itemId", &candidate) ||
        vm_net_mock_get_object_number_field(request, requestLen, "itemID", &candidate) ||
        vm_net_mock_get_object_number_field(request, requestLen, "itemid", &candidate) ||
        vm_net_mock_get_object_number_field(request, requestLen, "id", &candidate))
    {
        parsed.itemId = candidate;
        parsed.haveItemSelector = true;
    }

    if (vm_net_mock_next_request_object(request, requestLen, &offset, &object) &&
        object.kind == 7 && object.subtype == 8 && object.payloadLen != 0)
    {
        if (!parsed.haveItemSelector &&
            (vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "seq", &value) ||
             vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemseq", &value) ||
             vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemSeq", &value)))
        {
            if (value <= 0xffffu)
            {
                parsed.seq = (u16)value;
                parsed.haveItemSelector = true;
            }
        }
        if (parsed.itemId == 0 &&
            (vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemId", &candidate) ||
             vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemID", &candidate) ||
             vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "itemid", &candidate) ||
             vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "id", &candidate)))
        {
            parsed.itemId = candidate;
            parsed.haveItemSelector = true;
        }
    }

    if (parsedOut)
        *parsedOut = parsed;
    return true;
}

static bool vm_net_mock_parse_item_equip_swap_request(
    const u8 *request, u32 requestLen,
    vm_net_mock_item_equip_swap_request *parsedOut)
{
    u32 offset = 4;
    vm_net_mock_request_object swapObject;
    vm_net_mock_request_object companionObject;
    vm_net_mock_item_equip_swap_request parsed;

    if (parsedOut)
        memset(parsedOut, 0, sizeof(*parsedOut));
    memset(&parsed, 0, sizeof(parsed));

    if (request == NULL || requestLen < 14 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &swapObject) ||
        swapObject.major != 1 || swapObject.kind != 7 || swapObject.subtype != 9 ||
        !vm_net_mock_get_object_u16_field(swapObject.payload, swapObject.payloadLen,
                                          "body", &parsed.bodySeq) ||
        !vm_net_mock_get_object_u16_field(swapObject.payload, swapObject.payloadLen,
                                          "bag", &parsed.backpackSeq) ||
        parsed.bodySeq == 0 || parsed.backpackSeq == 0)
    {
        return false;
    }

    /* Some UI paths flush 2/10 in the same WT send, while the ordinary
     * equipment panel sends the valid 7/9 exchange by itself.  The 7/9 parser
     * consumes only body+bag, so 2/10 is an optional transport companion, not
     * part of the equipment-operation contract.  Any *other* trailing object
     * remains a separate feature-specific combo. */
    if (offset != requestLen)
    {
        if (!vm_net_mock_next_request_object(request, requestLen, &offset,
                                             &companionObject) ||
            companionObject.major != 1 || companionObject.kind != 2 ||
            companionObject.subtype != 10 || companionObject.payloadLen != 10 ||
            offset != requestLen)
        {
            return false;
        }
        parsed.hasActorOtherCompanion = true;
    }

    if (parsedOut)
        *parsedOut = parsed;
    return true;
}

static bool vm_net_mock_build_item_use_iteminfo_blob(u8 *out, u32 outCap,
                                                     u16 seq, u32 itemId,
                                                     u32 count, u32 *blobLenOut)
{
    u32 pos = 0;

    if (blobLenOut)
        *blobLenOut = 0;
    if (out == NULL || blobLenOut == NULL || itemId == 0)
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seq))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, itemId))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, count))
        return false;
    if (!vm_net_mock_seq_put_item_common_extra(
            out, outCap, &pos,
            vm_net_mock_backpack_item_id_uses_reservoir_count(itemId)
                ? (count == 0 ? 0 : 1)
                : (count > 255 ? 255 : (u8)count),
            0))
        return false;

    *blobLenOut = pos;
    return true;
}

/*
 * mmGameMstarWqvga.cbm:sub_11CE consumes 1/7/7 before it reaches the
 * ordinary scene business dispatcher.  With type=1, sub_D04 builds the one
 * received item row and sends it to TimerControl_ProcessItem, which is the
 * client-side additive/stacking path.  This is deliberately not 1/17/1:
 * that full-list object is only consumed while the backpack/shop list module
 * owns the network callback, so it cannot refresh an item bought from the
 * scene NPC service dialog.
 */
static bool vm_net_mock_append_backpack_item_add7_object(
    u8 *out, u32 outCap, u32 *pos, u16 seq, u32 itemId, u32 count)
{
    u8 itemInfo[64];
    u32 itemInfoLen = 0;
    u32 objectStart = 0;

    if (out == NULL || pos == NULL || seq == 0 || itemId == 0 || count == 0)
        return false;
    if (!vm_net_mock_build_item_use_iteminfo_blob(
            itemInfo, sizeof(itemInfo), seq, itemId, count, &itemInfoLen) ||
        itemInfoLen == 0 || itemInfoLen > 0xffffu)
    {
        return false;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 7,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "type", 1) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", itemInfo,
                                    (u16)itemInfoLen))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    printf("[info][network] mock_backpack_add item=%u seq=%u delta=%u iteminfo_len=%u response=7/7-type1 evidence=mmGame:0x11CE+0x0D04\n",
           itemId, seq, count, itemInfoLen);
    vm_autotest_note("mock_backpack_add item=%u seq=%u delta=%u iteminfo_len=%u response=7/7-type1 evidence=mmGame:0x11CE+0x0D04\n",
                     itemId, seq, count, itemInfoLen);
    return true;
}

static bool vm_net_mock_build_item_use_count_info_blob(u8 *out, u32 outCap,
                                                       u16 seq, u32 count,
                                                       u32 *blobLenOut)
{
    u32 pos = 0;

    if (blobLenOut)
        *blobLenOut = 0;
    if (out == NULL || blobLenOut == NULL || seq == 0)
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seq))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, count))
        return false;

    *blobLenOut = pos;
    return true;
}

static bool vm_net_mock_build_equipment_login_iteminfo_blob(
    u8 *out, u32 outCap, const vm_net_mock_role_state *role,
    u32 *blobLenOut, u8 *rowCountOut)
{
    vm_net_mock_role_service_state *serviceState = NULL;
    u32 pos = 0;
    u8 rowCount = 0;

    if (blobLenOut)
        *blobLenOut = 0;
    if (rowCountOut)
        *rowCountOut = 0;
    if (out == NULL || blobLenOut == NULL || role == NULL)
        return false;

    for (u8 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        u32 itemId = role->equippedItemIds[slot];

        if (itemId != 0 && vm_net_mock_find_equipment_catalog_item(itemId) != NULL)
            ++rowCount;
    }
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, rowCount))
        return false;

    serviceState = vm_net_mock_role_service_state_get(role);
    for (u8 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        u32 itemId = role->equippedItemIds[slot];
        u32 durability = VM_NET_MOCK_EQUIPMENT_DURABILITY_MAX;

        /* mmGameMstarWqvga.cbm:sub_D04 reads every 7/7 row as
         * seq(u16), itemId(u32), current-count(u32), and the common equipment
         * attributes.  For item ids >= 1000 it writes current-count to the
         * equipment current-durability field at item+272. */
        if (itemId == 0 || vm_net_mock_find_equipment_catalog_item(itemId) == NULL)
            continue;
        if (serviceState != NULL &&
            serviceState->equipmentItemIds[slot] == itemId &&
            serviceState->durability[slot] <= serviceState->durabilityMax[slot])
        {
            durability = serviceState->durability[slot];
        }
        if (!vm_net_mock_seq_put_i16(out, outCap, &pos, (u16)(slot + 1)) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, itemId) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, durability) ||
            !vm_net_mock_seq_put_item_common_extra(out, outCap, &pos, 0, 0))
        {
            return false;
        }
    }

    *blobLenOut = pos;
    if (rowCountOut)
        *rowCountOut = rowCount;
    return true;
}

static bool vm_net_mock_build_backpack_reservoir_count_info_blob(
    u8 *out, u32 outCap, const vm_net_mock_role_state *role,
    u32 *blobLenOut, u32 *rowCountOut)
{
    u32 pos = 0;
    u8 itemCount = vm_net_mock_role_backpack_count(role);
    u8 rowCount = 0;

    if (blobLenOut)
        *blobLenOut = 0;
    if (rowCountOut)
        *rowCountOut = 0;
    if (out == NULL || blobLenOut == NULL)
        return false;

    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        if (item->count != 0 &&
            vm_net_mock_backpack_item_id_uses_reservoir_count(item->itemId))
        {
            ++rowCount;
        }
    }
    if (rowCount == 0)
        return true;
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, rowCount))
        return false;
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        if (item->count == 0 ||
            !vm_net_mock_backpack_item_id_uses_reservoir_count(item->itemId))
        {
            continue;
        }
        if (!vm_net_mock_seq_put_i16(out, outCap, &pos, item->seq) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, item->count))
        {
            return false;
        }
    }

    *blobLenOut = pos;
    if (rowCountOut)
        *rowCountOut = rowCount;
    return true;
}

static u32 vm_net_mock_add_capped_u32(u32 value, u32 add)
{
    if (0xffffffffu - value < add)
        return 0xffffffffu;
    return value + add;
}

static u32 vm_net_mock_mul_capped_u32(u32 value, u32 count)
{
    uint64_t product = (uint64_t)value * (uint64_t)count;
    return product > 0xffffffffull ? 0xffffffffu : (u32)product;
}

static bool vm_net_mock_item_is_backpack_expand_card(u32 itemId,
                                                     const vm_net_mock_item_effect_catalog_item *effect)
{
    return itemId == VM_NET_MOCK_BACKPACK_EXPAND_ITEM_ID ||
           (effect != NULL && effect->itemId == VM_NET_MOCK_BACKPACK_EXPAND_ITEM_ID);
}

static bool vm_net_mock_shop_item_is_direct_backpack_expand(u8 type, u32 itemId)
{
    /*
     * mmShopMstarWqvga.cbm:sub_9DE case 14/3 success has a dedicated branch for
     * local purchase type 2 + item 806. The client does not add a usable row to
     * the backpack there; it expands capacity immediately.
     */
    return type == 2 && itemId == VM_NET_MOCK_BACKPACK_EXPAND_ITEM_ID;
}

static u8 vm_net_mock_shop_buy14_failure_result(u8 type)
{
    /*
     * mmShopMstarWqvga.cbm:sub_9DE only has an explicit handled failure branch
     * for result==2 on the W-coin buy flow (type==2). Returning 0 keeps the
     * local loading flag set and looks like a permanent network wait.
     */
    return type == 2 ? 2 : 0;
}

static u32 vm_net_mock_role_backpack_expand_usable_count(const vm_net_mock_role_state *role, u32 requestedCount)
{
    u32 current = 0;
    u32 room = 0;
    u32 usable = 0;

    if (role == NULL || requestedCount == 0)
        return 0;
    current = role->backpackCapacity;
    if (current < VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY)
        current = VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY;
    if (current >= VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT)
        return 0;
    room = VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT - current;
    usable = room / VM_NET_MOCK_BACKPACK_EXPAND_STEP;
    if (room % VM_NET_MOCK_BACKPACK_EXPAND_STEP)
        usable += 1;
    return requestedCount < usable ? requestedCount : usable;
}

static u32 vm_net_mock_role_expand_backpack_capacity(vm_net_mock_role_state *role, u32 useCount)
{
    u32 applied = 0;
    u32 newCapacity = 0;

    if (role == NULL || useCount == 0)
        return 0;
    applied = vm_net_mock_role_backpack_expand_usable_count(role, useCount);
    if (applied == 0)
        return 0;
    newCapacity = (u32)role->backpackCapacity +
                  vm_net_mock_mul_capped_u32(VM_NET_MOCK_BACKPACK_EXPAND_STEP, applied);
    if (newCapacity > VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT)
        newCapacity = VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT;
    role->backpackCapacity = (u8)newCapacity;
    return applied;
}

static u8 vm_net_mock_role_round_backpack_capacity_for_count(u32 itemCount)
{
    u32 rounded = 0;

    if (itemCount < VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY)
        itemCount = VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY;
    if (itemCount >= VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT)
        return VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT;
    rounded = ((itemCount + VM_NET_MOCK_BACKPACK_EXPAND_STEP - 1) /
               VM_NET_MOCK_BACKPACK_EXPAND_STEP) *
              VM_NET_MOCK_BACKPACK_EXPAND_STEP;
    if (rounded > VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT)
        rounded = VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT;
    return (u8)rounded;
}

static void vm_net_mock_role_migrate_legacy_backpack_capacity(vm_net_mock_role_state *role)
{
    u32 itemCount = 0;

    if (role == NULL || role->backpackCapacity != VM_NET_MOCK_BACKPACK_LEGACY_MAX_ITEMS)
        return;
    itemCount = role->backpackItemCount;
    if (itemCount > VM_NET_MOCK_BACKPACK_MAX_ITEMS)
        itemCount = VM_NET_MOCK_BACKPACK_MAX_ITEMS;
    role->backpackCapacity = vm_net_mock_role_round_backpack_capacity_for_count(itemCount);
}

static void vm_net_mock_role_apply_item_effect(vm_net_mock_role_state *role,
                                               u32 hp, u32 mp, u32 exp,
                                               u32 count)
{
    if (role == NULL || count == 0)
        return;

    vm_net_mock_role_sync_derived_vitals(role);
    if (hp != 0)
    {
        uint64_t add = (uint64_t)hp * (uint64_t)count;
        u32 capped = add > 0xffffffffull ? 0xffffffffu : (u32)add;
        role->hp = vm_net_mock_min_u32(vm_net_mock_add_capped_u32(role->hp, capped), role->hpMax);
    }
    if (mp != 0)
    {
        uint64_t add = (uint64_t)mp * (uint64_t)count;
        u32 capped = add > 0xffffffffull ? 0xffffffffu : (u32)add;
        role->mp = vm_net_mock_min_u32(vm_net_mock_add_capped_u32(role->mp, capped), role->mpMax);
    }
    if (exp != 0)
    {
        uint64_t add = (uint64_t)exp * (uint64_t)count;
        u32 capped = add > 0xffffffffull ? 0xffffffffu : (u32)add;
        vm_net_mock_role_add_exp(role, capped);
    }
}

static u32 vm_net_mock_build_item_use_hint_response(u8 *out, u32 outCap, const char *hint)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 16, 2, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 4))
        return 0;
    if (!vm_net_mock_put_object_string(out, outCap, &pos, "hint", hint ? hint : "OK"))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

static u32 vm_net_mock_build_item_use_response(const u8 *request, u32 requestLen,
                                               u8 *out, u32 outCap)
{
    vm_net_mock_item_use_request parsed;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *item = NULL;
    const vm_net_mock_item_effect_catalog_item *effect = NULL;
    u32 itemId = 0;
    u16 seq = 0;
    u32 hp = 0;
    u32 mp = 0;
    u32 exp = 0;
    u32 hpApplied = 0;
    u32 mpApplied = 0;
    u32 useCount = 0;
    u32 consumedCount = 0;
    u32 reservoirBefore = 0;
    u32 expandedCount = 0;
    u8 oldCapacity = 0;
    u8 newCapacity = 0;
    u32 remaining = 0;
    bool consumed = false;
    bool applied = false;
    bool reservoirItem = false;
    bool capacityExpanded = false;
    u8 itemInfo[64];
    u32 itemInfoLen = 0;
    u8 countInfo[32];
    u32 countInfoLen = 0;
    u8 itemUseType = 1;
    bool suppressUseSuccessPopup = false;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_parse_item_use_request(request, requestLen, &parsed))
        return 0;
    useCount = parsed.count ? parsed.count : 1;

    role = vm_net_mock_active_role();
    if (role == NULL)
        return vm_net_mock_build_item_use_hint_response(out, outCap, "item unavailable");
    oldCapacity = role->backpackCapacity;

    item = vm_net_mock_role_find_backpack_item(role, parsed.itemId, parsed.seq);
    if (item == NULL && !parsed.haveItemSelector && parsed.haveEffect)
        item = vm_net_mock_role_find_backpack_item_by_effect(role, parsed.hp, parsed.mp, parsed.exp);
    if (item != NULL)
    {
        itemId = item->itemId;
        seq = item->seq;
    }
    else
    {
        itemId = parsed.itemId;
        seq = parsed.seq;
    }

    effect = vm_net_mock_find_item_effect_catalog_item(itemId);
    reservoirItem = vm_net_mock_item_effect_is_reservoir(effect);
    /* item.dsh category 14 uses num=1 as the small-horn consume amount after
     * the chat input is accepted.  It is not an HP-effect value. */
    if (itemId == VM_NET_MOCK_SMALL_HORN_ITEM_ID)
    {
        parsed.hp = 0;
        parsed.mp = 0;
        parsed.exp = 0;
    }
    if (vm_net_mock_item_effect_is_usable(effect))
    {
        hp = effect->hp;
        mp = effect->mp;
        exp = effect->exp;
    }
    if (hp == 0)
        hp = parsed.hp;
    if (mp == 0)
        mp = parsed.mp;
    if (exp == 0)
        exp = parsed.exp;
    if (reservoirItem && item == NULL)
        return vm_net_mock_build_item_use_hint_response(out, outCap, "item unavailable");

    if (reservoirItem && item != NULL)
    {
        u32 missingHp = 0;
        u32 missingMp = 0;

        vm_net_mock_role_sync_derived_vitals(role);
        reservoirBefore = item->count;
        missingHp = role->hpMax > role->hp ? role->hpMax - role->hp : 0;
        missingMp = role->mpMax > role->mp ? role->mpMax - role->mp : 0;
        consumedCount = vm_net_mock_item_effect_plan_reservoir_restore(
            effect, reservoirBefore, missingHp, missingMp, &hpApplied, &mpApplied);
        remaining = reservoirBefore;
        if (consumedCount != 0)
            consumed = vm_net_mock_role_consume_backpack_item(
                role, itemId, seq, consumedCount, &remaining);
        else
            consumed = true;
        if (consumed)
        {
            role->hp = vm_net_mock_min_u32(
                vm_net_mock_add_capped_u32(role->hp, hpApplied), role->hpMax);
            role->mp = vm_net_mock_min_u32(
                vm_net_mock_add_capped_u32(role->mp, mpApplied), role->mpMax);
            applied = hpApplied != 0 || mpApplied != 0;
        }
    }
    else if (vm_net_mock_item_is_backpack_expand_card(itemId, effect))
    {
        consumedCount = vm_net_mock_role_backpack_expand_usable_count(role, useCount);
        if (consumedCount == 0)
            return vm_net_mock_build_item_use_hint_response(out, outCap, "capacity max");
    }
    else
    {
        consumedCount = useCount;
    }

    if (!reservoirItem && itemId != 0 && consumedCount != 0)
        consumed = vm_net_mock_role_consume_backpack_item(role, itemId, seq, consumedCount, &remaining);

    if (consumed && vm_net_mock_item_is_backpack_expand_card(itemId, effect))
    {
        expandedCount = vm_net_mock_role_expand_backpack_capacity(role, consumedCount);
        if (expandedCount != 0)
        {
            capacityExpanded = true;
            applied = true;
        }
    }

    if (!reservoirItem && consumed && (hp != 0 || mp != 0 || exp != 0))
    {
        vm_net_mock_role_apply_item_effect(role, hp, mp, exp, consumedCount);
        hpApplied = hp != 0 ? vm_net_mock_mul_capped_u32(hp, consumedCount) : 0;
        mpApplied = mp != 0 ? vm_net_mock_mul_capped_u32(mp, consumedCount) : 0;
        applied = true;
    }

    newCapacity = role->backpackCapacity;

    if (applied || consumed)
        vm_net_mock_role_db_save("item-use");

    if (itemId == 0)
    {
        u32 hintLen = vm_net_mock_build_item_use_hint_response(out, outCap,
                                                               "item unavailable");
        vm_autotest_note("mock_item_use type=%u item=0 seq=0 count=%u hp=%u mp=%u exp=%u applied=%u consumed=0 response=16/2-hint evidence=runtime:wt7/1 mmGame:0x11CE\n",
                         parsed.type, parsed.count, hp, mp, exp, applied ? 1 : 0);
        return hintLen;
    }

    itemUseType = parsed.type ? parsed.type : 1;
    suppressUseSuccessPopup = itemId == VM_NET_MOCK_SMALL_HORN_ITEM_ID;
    /*
     * JianghuOL.CBE:0x1033544 handles 7/1 as the original item-use success
     * acknowledgement.  When the client has a pending use row, result=1 calls
     * the item-manager operation at +56 with type/id/count=1; this is the path
     * that also updates the occupied-slot counter when the stack reaches zero.
     *
     * Small-horn chat is submitted while the message screen is active.  The
     * same 7/1 success branch also calls ui_show_message_box("使用成功",...,10).
     * That screen does not run the scene toast countdown, leaving the bar on
     * screen indefinitely.  Its following 7/11 refresh already clears the
     * pending-use flag at R9+38036, while 7/7 performs the row refresh/removal,
     * so item 807 must omit only this popup-producing acknowledgement.
     */
    if (!suppressUseSuccessPopup)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 1, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1))
            return 0;
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", itemUseType))
            return 0;
        if (!vm_net_mock_put_object_u16(out, outCap, &pos, "id", (u16)itemId))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }

    if (!reservoirItem)
    {
        /*
         * mmGame:0xD04 type=2 invokes the ordinary selected-row removal path.
         * Do not send it for consumeMode=2 flasks: JianghuOL.CBE:0x10336CA
         * updates their HP/MP reservoir directly from 7/11 and removes the row
         * only when that value reaches zero.
         */
        if (!vm_net_mock_build_item_use_iteminfo_blob(itemInfo, sizeof(itemInfo),
                                                      seq, itemId, remaining,
                                                      &itemInfoLen))
            return 0;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 7, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "type", 2))
            return 0;
        if (!vm_net_mock_put_object_raw(out, outCap, &pos, "iteminfo", itemInfo, (u16)itemInfoLen))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }

    /*
     * JianghuOL.CBE:0x1033544 handles 7/11 and 7/12 by reading an "info"
     * stream of row_count, seq, and new_count, then writing the backpack row.
     * This path is reached before the mmGame callback that ignores kind 17.
     */
    if (seq != 0)
    {
        if (!vm_net_mock_build_item_use_count_info_blob(countInfo, sizeof(countInfo),
                                                        seq, remaining, &countInfoLen))
            return 0;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 11, &objectStart))
            return 0;
        if (!vm_net_mock_put_object_raw(out, outCap, &pos, "info", countInfo, (u16)countInfoLen))
            return 0;
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }
    if (capacityExpanded)
    {
        if (!vm_net_mock_append_backpack_items_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
    }
    vm_net_mock_finish_wt_packet(out, pos, objectCount);

    printf("[info][network] mock_item_use item=%u seq=%u count=%u mode=%u reserve=%u->%u consumed=%u hp=%u/%u mp=%u/%u exp=%u cap=%u->%u expand=%u applied=%u consumed_ok=%u refresh=%s resp=%u evidence=JianghuOL.CBE:0x1033544+item.dsh:consumeMode\n",
           itemId, seq, parsed.count, reservoirItem ? 2u : (effect ? effect->consumeMode : 0u),
           reservoirBefore, remaining, consumedCount, hpApplied, hp, mpApplied, mp, exp,
           oldCapacity, newCapacity, expandedCount,
           applied ? 1 : 0, consumed ? 1 : 0,
           suppressUseSuccessPopup ? "7/7+7/11-small-horn-no-popup" :
           (capacityExpanded ? "7/1+7/7+7/11+17/1-followup" :
               (reservoirItem ? "7/1+7/11-reservoir" : "7/1+7/7+7/11")),
           pos);
    vm_autotest_note("mock_item_use item=%u seq=%u count=%u mode=%u reserve=%u->%u consumed=%u hp=%u/%u mp=%u/%u exp=%u cap=%u->%u expand=%u applied=%u consumed_ok=%u response=%s evidence=runtime:wt7/1 JianghuOL.CBE:0x1033544 item.dsh:consumeMode\n",
                     itemId, seq, parsed.count, reservoirItem ? 2u : (effect ? effect->consumeMode : 0u),
                     reservoirBefore, remaining, consumedCount, hpApplied, hp, mpApplied, mp, exp,
                     oldCapacity, newCapacity, expandedCount,
                     applied ? 1 : 0, consumed ? 1 : 0,
                      suppressUseSuccessPopup ? "7/7-type2+7/11-info-small-horn-no-popup" :
                       (capacityExpanded ? "7/1-use-ok+7/7-type2+7/11-info+17/1-followup" :
                           (reservoirItem ? "7/1-use-ok+7/11-reservoir" :
                                           "7/1-use-ok+7/7-type2+7/11-info")));
    return pos;
}

static u32 vm_net_mock_build_item_discard_response(const u8 *request, u32 requestLen,
                                                   u8 *out, u32 outCap)
{
    vm_net_mock_item_discard_request parsed;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *item = NULL;
    u32 itemId = 0;
    u16 seq = 0;
    u32 discardCount = 0;
    u32 remaining = 0;
    bool consumed = false;
    u8 result = 2;
    u8 countInfo[32];
    u32 countInfoLen = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_parse_item_discard_request(request, requestLen, &parsed))
        return 0;

    role = vm_net_mock_active_role();
    if (role != NULL)
    {
        item = vm_net_mock_role_find_backpack_item(role, parsed.itemId, parsed.seq);
        if (item == NULL && parsed.seq != 0)
            item = vm_net_mock_role_find_backpack_item(role, 0, parsed.seq);
        if (item == NULL && parsed.itemId != 0)
            item = vm_net_mock_role_find_backpack_item(role, parsed.itemId, 0);
        if (item != NULL)
        {
            itemId = item->itemId;
            seq = item->seq;
            discardCount = parsed.count ? parsed.count : item->count;
            if (discardCount == 0)
                discardCount = item->count;
            consumed = vm_net_mock_role_consume_backpack_item(role, itemId, seq,
                                                              discardCount, &remaining);
            if (consumed)
            {
                result = 1;
                vm_net_mock_role_db_save("item-discard");
            }
        }
        else
        {
            itemId = parsed.itemId;
            seq = parsed.seq;
        }
    }

    /*
     * JianghuOL.CBE:0x1033544 handles 7/4 as the item-operation completion
     * branch and clears the waiting flag.  The backpack UI callback is the
     * proven mmGame:0x418C path, so a successful discard also sends a full
     * 17/1 list rebuild plus 7/42 book filler.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 4, &objectStart))
        return 0;
    if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
        return 0;
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    objectCount += 1;

    if (consumed)
    {
        if (!vm_net_mock_append_backpack_items_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
        if (!vm_net_mock_append_books42_object(out, outCap, &pos))
            return 0;
        objectCount += 1;
        if (seq != 0)
        {
            if (!vm_net_mock_build_item_use_count_info_blob(countInfo, sizeof(countInfo),
                                                            seq, remaining, &countInfoLen))
                return 0;
            if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 11, &objectStart))
                return 0;
            if (!vm_net_mock_put_object_raw(out, outCap, &pos, "info", countInfo, (u16)countInfoLen))
                return 0;
            vm_net_mock_finish_wt_object(out, objectStart, pos);
            objectCount += 1;
        }
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_item_discard item=%u seq=%u count=%u remaining=%u result=%u refresh=%s resp=%u\n",
           itemId, seq, discardCount, remaining, result,
           consumed ? "7/4+17/1+7/42+7/11" : "7/4-fail", pos);
    vm_autotest_note("mock_item_discard item=%u seq=%u count=%u remaining=%u result=%u response=%s evidence=runtime:wt7/4 JianghuOL.CBE:0x1033544 mmGame:0x418C\n",
                     itemId, seq, discardCount, remaining, result,
                     consumed ? "7/4+17/1+7/42+7/11" : "7/4-fail");
    return pos;
}

static bool vm_net_mock_append_backpack_items_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u8 itemInfo[1024];
    u32 itemInfoLen = 0;
    u32 rowCount = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u16 capacity = role ? role->backpackCapacity : VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY;

    if (out == NULL || pos == NULL)
        return false;
    memset(itemInfo, 0, sizeof(itemInfo));
    if (!vm_net_mock_build_backpack_iteminfo_blob(itemInfo, sizeof(itemInfo), role,
                                                 &itemInfoLen, &rowCount))
        return false;
    if (itemInfoLen == 0 || itemInfoLen > 0xffff)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 17, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u16(out, outCap, pos, "maxnum", capacity))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", itemInfo, (u16)itemInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    printf("[info][network] mock_backpack_items role=%u capacity=%u rows=%u iteminfo_len=%u\n",
           role ? role->roleId : 0,
           capacity,
           rowCount,
           itemInfoLen);
    vm_autotest_note("mock_backpack_items role=%u capacity=%u rows=%u iteminfo_len=%u evidence=mmGame:0x418C\n",
                     role ? role->roleId : 0,
                     capacity,
                     rowCount,
                     itemInfoLen);
    return true;
}

static bool vm_net_mock_append_shop17_items_object(u8 *out, u32 outCap, u32 *pos,
                                                   u32 *rowCountOut, u32 *itemInfoLenOut)
{
    u32 objectStart = 0;
    u8 itemInfo[32768];
    u32 itemInfoLen = 0;
    u32 rowCount = 0;

    if (rowCountOut)
        *rowCountOut = 0;
    if (itemInfoLenOut)
        *itemInfoLenOut = 0;
    if (out == NULL || pos == NULL)
        return false;
    memset(itemInfo, 0, sizeof(itemInfo));
    if (!vm_net_mock_build_shop17_iteminfo_blob(itemInfo, sizeof(itemInfo), &itemInfoLen, &rowCount))
        return false;
    if (itemInfoLen == 0 || itemInfoLen > 0xffff)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 17, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", itemInfo, (u16)itemInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    if (rowCountOut)
        *rowCountOut = rowCount;
    if (itemInfoLenOut)
        *itemInfoLenOut = itemInfoLen;
    return true;
}

static bool vm_net_mock_append_backpack_grid_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u8 itemInfo[1024];
    u32 itemInfoLen = 0;
    u32 gridCount = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (out == NULL || pos == NULL)
        return false;
    memset(itemInfo, 0, sizeof(itemInfo));
    if (!vm_net_mock_build_backpack_grid_iteminfo_blob(itemInfo, sizeof(itemInfo), role,
                                                      &itemInfoLen, &gridCount))
        return false;
    if (gridCount == 0 || itemInfoLen > 0xffff)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 30, 21, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "result", 1))
        return false;
    if (!vm_net_mock_put_object_u8(out, outCap, pos, "gridnum", (u8)gridCount))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", itemInfo, (u16)itemInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    printf("[info][network] mock_backpack_grid role=%u kind=30 subtype=21 gridnum=%u iteminfo_len=%u\n",
           role ? role->roleId : 0,
           gridCount,
           itemInfoLen);
    vm_autotest_note("mock_backpack_grid role=%u kind=30 subtype=21 gridnum=%u iteminfo_len=%u evidence=JianghuOL:0x1039952\n",
                     role ? role->roleId : 0,
                     gridCount,
                     itemInfoLen);
    return true;
}

static bool vm_net_mock_append_backpack_reservoir_counts_object(
    u8 *out, u32 outCap, u32 *pos, bool *appendedOut)
{
    u32 objectStart = 0;
    u8 info[512];
    u32 infoLen = 0;
    u32 rowCount = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (appendedOut)
        *appendedOut = false;
    if (out == NULL || pos == NULL)
        return false;
    memset(info, 0, sizeof(info));
    if (!vm_net_mock_build_backpack_reservoir_count_info_blob(
            info, sizeof(info), role, &infoLen, &rowCount))
    {
        return false;
    }
    if (rowCount == 0)
        return true;
    if (infoLen == 0 || infoLen > 0xffff)
        return false;

    /*
     * JianghuOL.CBE:0x01033544 handles 7/11 after 30/21 has inserted the
     * sequence rows.  For 802/803 it writes the u32 value to the HP/MP
     * reservoir field (+4/+8) without changing the visible quantity of 1.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 11, &objectStart))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "info", info, (u16)infoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    if (appendedOut)
        *appendedOut = true;

    printf("[info][network] mock_backpack_reservoir_seed role=%u rows=%u info_len=%u response=7/11 evidence=JianghuOL.CBE:0x1033544\n",
           role ? role->roleId : 0, rowCount, infoLen);
    vm_autotest_note("mock_backpack_reservoir_seed role=%u rows=%u info_len=%u response=7/11 evidence=JianghuOL.CBE:0x1033544\n",
                     role ? role->roleId : 0, rowCount, infoLen);
    return true;
}

static bool vm_net_mock_append_equipment_login_object(
    u8 *out, u32 outCap, u32 *pos, u8 *rowCountOut)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u8 itemInfo[512];
    u32 itemInfoLen = 0;
    u32 objectStart = 0;
    u8 rowCount = 0;

    if (rowCountOut)
        *rowCountOut = 0;
    if (out == NULL || pos == NULL || role == NULL)
        return false;
    memset(itemInfo, 0, sizeof(itemInfo));
    if (!vm_net_mock_build_equipment_login_iteminfo_blob(
            itemInfo, sizeof(itemInfo), role, &itemInfoLen, &rowCount) ||
        itemInfoLen == 0 || itemInfoLen > 0xffffu)
    {
        return false;
    }

    /* mmGameMstarWqvga.cbm:sub_D04(0x0D04) dispatches 7/7 type=2 rows to
     * the main item manager's +104 operation.  JianghuOL.CBE:0x01032B8A
     * copies each row, preserves the original DSH category in item+283,
     * changes item+282 to category 15, and inserts it into the equipment
     * list.  Passing -1 on this bootstrap path deliberately does not remove a
     * pending backpack row. */
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 7, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "type", 2) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo",
                                    itemInfo, (u16)itemInfoLen))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    if (rowCountOut)
        *rowCountOut = rowCount;

    printf("[info][network] mock_equipment_login role=%u rows=%u iteminfo_len=%u response=7/7-type2 evidence=mmGame:0x0D04+JianghuOL:0x01032B8A\n",
           role->roleId, rowCount, itemInfoLen);
    vm_autotest_note("mock_equipment_login role=%u rows=%u iteminfo_len=%u response=7/7-type2 evidence=mmGame:0x0D04+JianghuOL:0x01032B8A\n",
                     role->roleId, rowCount, itemInfoLen);
    return true;
}

static bool vm_net_mock_append_backpack_role_grid_main_objects(u8 *out, u32 outCap, u32 *pos, u8 *objectCount)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    if (out == NULL || pos == NULL || objectCount == NULL)
        return false;
    if (role == NULL)
        return true;
    if (g_netMockBackpackGridSeededRoleId != role->roleId)
    {
        bool appendedReservoirCounts = false;
        u8 equipmentRows = 0;

        if (vm_net_mock_role_backpack_count(role) != 0)
        {
            if (!vm_net_mock_append_backpack_grid_object(out, outCap, pos))
                return false;
            *objectCount = (u8)(*objectCount + 1);
            if (!vm_net_mock_append_backpack_reservoir_counts_object(
                    out, outCap, pos, &appendedReservoirCounts))
            {
                return false;
            }
            if (appendedReservoirCounts)
                *objectCount = (u8)(*objectCount + 1);
        }
        if (!vm_net_mock_append_equipment_login_object(
                out, outCap, pos, &equipmentRows))
            return false;
        *objectCount = (u8)(*objectCount + 1);
        g_netMockBackpackGridSeededRoleId = role->roleId;
    }
    return true;
}

static u32 vm_net_mock_build_backpack_items_response(u8 *out, u32 outCap)
{
    u32 pos = 5;

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_backpack_items_object(out, outCap, &pos))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);

    return pos;
}

static u32 vm_net_mock_build_backpack_open_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;

    if (outCap < pos)
        return 0;
    /*
     * mmGameMstarWqvga.cbm:sub_2434 opens the backpack component and sends
     * 7/42. Its registered network parser is sub_418C, which handles both
     * 17/1 iteminfo and 7/42 book info while the backpack component is active.
     */
    if (!vm_net_mock_append_backpack_items_object(out, outCap, &pos))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static u32 vm_net_mock_build_backpack_items_books_combo_response(const u8 *request, u32 requestLen,
                                                                u8 *out, u32 outCap)
{
    u16 itemsPayloadLen = 0;
    u32 responseLen = 0;

    if (!vm_net_mock_is_backpack_items_books_combo_request(request, requestLen, &itemsPayloadLen))
        return 0;

    responseLen = vm_net_mock_build_backpack_open_response(out, outCap);
    if (responseLen)
    {
        printf("[info][network] mock_backpack_items_books_combo len=%u items_payload=%u response=17/1+7/42\n",
               requestLen,
               itemsPayloadLen);
        vm_autotest_note("mock_backpack_items_books_combo len=%u items_payload=%u response=17/1+7/42 evidence=mmGame:0x418C runtime=wt17/1-len25\n",
                         requestLen,
                         itemsPayloadLen);
    }
    return responseLen;
}

static u32 vm_net_mock_build_shop_items_books_combo_response(const u8 *request, u32 requestLen,
                                                            u8 *out, u32 outCap)
{
    u16 itemsPayloadLen = 0;
    u32 pos = 5;
    u8 objectCount = 0;
    u32 rowCount = 0;
    u32 itemInfoLen = 0;
    char ids[160];

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_is_backpack_items_books_combo_request(request, requestLen, &itemsPayloadLen))
        return 0;
    if (itemsPayloadLen == 0)
        return 0;

    /*
     * NPC dialog buy reaches the mmGame list parser at 0x418C.  That parser's
     * 17/1 branch loads item.dsh/equip.dsh locally and expects iteminfo rows of
     * itemId + common item-extra; returning the normal backpack one-row 17/1
     * keeps the visible shop stuck on 传送石 only.
     */
    if (!vm_net_mock_append_shop17_items_object(out, outCap, &pos, &rowCount, &itemInfoLen))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    g_netMockShop17ListPending = 0;
    vm_net_mock_format_shop17_ids(8, ids, sizeof(ids));
    printf("[info][network] mock_shop_items_books_combo rows=%u iteminfo_len=%u first=%u ids=%s\n",
           rowCount,
           itemInfoLen,
           vm_net_mock_shop17_first_item_id(),
           ids);
    vm_autotest_note("mock_shop_items_books_combo len=%u items_payload=%u rows=%u iteminfo_len=%u first=%u response=17/1+7/42 evidence=mmGame:0x418C runtime=npc-buy-wt17/1-len25\n",
                     requestLen,
                     itemsPayloadLen,
                     rowCount,
                     itemInfoLen,
                     vm_net_mock_shop17_first_item_id());
    return pos;
}

static u32 vm_net_mock_build_shop_items17_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u32 rowCount = 0;
    u32 itemInfoLen = 0;
    char ids[160];

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_shop17_items_object(out, outCap, &pos, &rowCount, &itemInfoLen))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, 1);
    g_netMockShop17ListPending = 0;
    vm_net_mock_format_shop17_ids(8, ids, sizeof(ids));
    printf("[info][network] mock_shop_items17 rows=%u iteminfo_len=%u first=%u ids=%s\n",
           rowCount,
           itemInfoLen,
           vm_net_mock_shop17_first_item_id(),
           ids);
    vm_autotest_note("mock_shop_items17 rows=%u iteminfo_len=%u first=%u response=17/1 evidence=mmGame:0x418C runtime=shop-context-empty-17/1\n",
                     rowCount,
                     itemInfoLen,
                     vm_net_mock_shop17_first_item_id());
    return pos;
}

static u32 vm_net_mock_build_shop_items_books_response(u8 *out, u32 outCap)
{
    u32 pos = 5;
    u8 objectCount = 0;
    u32 rowCount = 0;
    u32 itemInfoLen = 0;
    char ids[160];

    if (outCap < pos)
        return 0;
    if (!vm_net_mock_append_shop17_items_object(out, outCap, &pos, &rowCount, &itemInfoLen))
        return 0;
    objectCount += 1;
    if (!vm_net_mock_append_books42_object(out, outCap, &pos))
        return 0;
    objectCount += 1;
    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    g_netMockShop17ListPending = 0;
    vm_net_mock_format_shop17_ids(8, ids, sizeof(ids));
    printf("[info][network] mock_shop_items_books rows=%u iteminfo_len=%u first=%u ids=%s\n",
           rowCount,
           itemInfoLen,
           vm_net_mock_shop17_first_item_id(),
           ids);
    vm_autotest_note("mock_shop_items_books rows=%u iteminfo_len=%u first=%u response=17/1+7/42 evidence=mmGame:0x418C runtime=shop-context-7/42\n",
                     rowCount,
                     itemInfoLen,
                     vm_net_mock_shop17_first_item_id());
    return pos;
}

static const char *vm_net_mock_default_scene_name(void);
static bool vm_net_mock_scene_is_penglai01(const char *scene);
static bool vm_net_mock_scene_is_penglai02(const char *scene);
static bool vm_net_mock_scene_is_penglai03(const char *scene);
static bool vm_net_mock_scene_is_penglai04(const char *scene);
static bool vm_net_mock_scene_is_penglai_transfer_scene(const char *scene);
static bool vm_net_mock_scene_is_c00_penglai03(const char *scene);
static bool vm_net_mock_scene_is_taohuadao01(const char *scene);

static vm_net_mock_role_db_file g_vm_net_mock_role_db;
static bool g_vm_net_mock_role_db_loaded = false;
static bool g_vm_net_mock_role_db_valid = false;
static bool g_vm_net_mock_role_position_dirty = false;
static u32 g_vm_net_mock_battle_rewarded_serial = 0;
static u32 g_vm_net_mock_battle_rewarded_exp = 0;
static u32 g_vm_net_mock_battle_rewarded_drop_item = 0;
static u16 g_vm_net_mock_battle_rewarded_drop_seq = 0;
static u32 g_vm_net_mock_battle_rewarded_drop_count = 0;
static u32 g_vm_net_mock_battle_enemy_id_current = VM_NET_MOCK_BATTLE_POISON_SLIME_ID;
static u32 g_vm_net_mock_battle_role_id_current = VM_NET_MOCK_ROLE_DEFAULT_ID;
static u32 g_vm_net_mock_battle_reward_rng = 0;
static u32 g_vm_net_mock_battle_settlement_sent_serial = 0;
static u32 g_vm_net_mock_battle_drop_refresh_sent_serial = 0;
static u32 g_vm_net_mock_battle_recovered_serial = 0;
static char g_vm_net_mock_scene_moveinfo_npc_pending_scene[64];
static bool g_vm_net_mock_scene_moveinfo_npc_pending = false;
static char g_vm_net_mock_scene_moveinfo_npc_seeded_scene[64];
static bool g_vm_net_mock_scene_moveinfo_npc_seeded = false;

static bool vm_net_mock_read_current_player_grid(u32 *nodeOut, u32 *actorIdOut,
                                                 u16 *gridXOut, u16 *gridYOut,
                                                 u16 *targetXOut, u16 *targetYOut);
static bool vm_net_mock_snapshot_current_player_pos(const char *reason);
static bool vm_net_mock_scene_names_equal_loose(const char *a, const char *b);

static void vm_net_mock_reset_scene_moveinfo_npc_seed_if_needed(const char *scene)
{
    if (g_vm_net_mock_scene_moveinfo_npc_seeded &&
        (scene == NULL ||
         g_vm_net_mock_scene_moveinfo_npc_seeded_scene[0] == 0 ||
         !vm_net_mock_scene_names_equal_loose(g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
                                              scene)))
    {
        g_vm_net_mock_scene_moveinfo_npc_seeded = false;
        g_vm_net_mock_scene_moveinfo_npc_seeded_scene[0] = 0;
    }
    if (g_vm_net_mock_scene_moveinfo_npc_pending &&
        (scene == NULL ||
         g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] == 0 ||
         !vm_net_mock_scene_names_equal_loose(g_vm_net_mock_scene_moveinfo_npc_pending_scene,
                                              scene)))
    {
        g_vm_net_mock_scene_moveinfo_npc_pending = false;
        g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] = 0;
    }
}

static void vm_net_mock_mark_scene_moveinfo_npc_seed_pending(const char *scene)
{
    if (scene == NULL || scene[0] == 0)
        return;
    g_vm_net_mock_scene_moveinfo_npc_pending = true;
    snprintf(g_vm_net_mock_scene_moveinfo_npc_pending_scene,
             sizeof(g_vm_net_mock_scene_moveinfo_npc_pending_scene),
             "%s", scene);
    g_vm_net_mock_scene_moveinfo_npc_seeded = false;
    g_vm_net_mock_scene_moveinfo_npc_seeded_scene[0] = 0;
}

static bool vm_net_mock_is_scene_moveinfo_npc_seed_request(const char *scene,
                                                           const u8 *moveInfo,
                                                           u16 moveInfoLen)
{
    if (!g_vm_net_mock_scene_moveinfo_npc_pending)
        return false;
    if (scene == NULL || scene[0] == 0 ||
        g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] == 0 ||
        !vm_net_mock_scene_names_equal_loose(g_vm_net_mock_scene_moveinfo_npc_pending_scene,
                                             scene))
    {
        return false;
    }
    if (moveInfo == NULL || moveInfoLen != 10)
        return false;
    return true;
}

static bool vm_net_mock_str_ends_with(const char *text, const char *suffix)
{
    size_t textLen = text ? strlen(text) : 0;
    size_t suffixLen = suffix ? strlen(suffix) : 0;
    if (suffixLen == 0 || textLen < suffixLen)
        return false;
    return strcmp(text + textLen - suffixLen, suffix) == 0;
}

static bool vm_net_mock_scene_name_has_path_separator(const char *scene)
{
    if (scene == NULL)
        return true;
    for (const char *p = scene; *p; ++p)
    {
        if (*p == '/' || *p == '\\' || *p == ':' || (u8)*p < 0x20)
            return true;
    }
    return false;
}

static bool vm_net_mock_scene_name_is_download_key(const char *scene)
{
    /* DSH map names are server-provided resource keys (for example
     * `01桃花岛_01.sce`), not necessarily the older c-prefixed scene form.
     * Reject only empty/path-bearing values; the normal resource-existence
     * check remains the authoritative validation step. */
    return scene != NULL && scene[0] != 0 && !vm_net_mock_scene_name_has_path_separator(scene);
}

static bool vm_net_mock_open_server_scene_resource(const char *scene,
                                                   FILE **fpOut,
                                                   char *pathOut,
                                                   size_t pathOutCap)
{
    static const char *pathFormats[] = {
        "../web/fs/JHOnlineData/%s%s",
        "web/fs/JHOnlineData/%s%s"
    };
    char candidate[1200];

    if (fpOut)
        *fpOut = NULL;
    if (pathOut && pathOutCap != 0)
        pathOut[0] = 0;
    if (scene == NULL || scene[0] == 0 || vm_net_mock_scene_name_has_path_separator(scene))
        return false;

    for (u32 extPass = 0; extPass < 2; ++extPass)
    {
        const char *suffix = extPass == 0 ? "" : ".sce";
        if (extPass != 0 && vm_net_mock_str_ends_with(scene, ".sce"))
            continue;
        if (g_vm_net_mock_resource_dir[0] != 0)
        {
            char resourceName[128];
            FILE *fp = NULL;
            snprintf(resourceName, sizeof(resourceName), "%s%s", scene, suffix);
            if (vm_net_mock_build_configured_resource_path(resourceName, candidate,
                                                           sizeof(candidate)))
            {
                fp = vm_net_mock_fopen_game_path(candidate, "rb");
                if (fp != NULL)
                {
                    if (pathOut && pathOutCap != 0)
                        snprintf(pathOut, pathOutCap, "%s", candidate);
                    if (fpOut)
                        *fpOut = fp;
                    else
                        fclose(fp);
                    return true;
                }
            }
        }
        for (u32 i = 0; i < sizeof(pathFormats) / sizeof(pathFormats[0]); ++i)
        {
            snprintf(candidate, sizeof(candidate), pathFormats[i], scene, suffix);
            FILE *fp = vm_net_mock_fopen_game_path(candidate, "rb");
            if (fp == NULL)
                continue;
            if (pathOut && pathOutCap != 0)
                snprintf(pathOut, pathOutCap, "%s", candidate);
            if (fpOut)
            {
                *fpOut = fp;
            }
            else
            {
                fclose(fp);
            }
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_scene_resource_legacy_alias(const char *scene,
                                                    char *out,
                                                    size_t outCap)
{
    const char *base = scene;
    const char *suffix = NULL;
    const char *end = NULL;
    size_t baseLen = 0;
    size_t stemLen = 0;

    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (scene == NULL || scene[0] == 0 ||
        vm_net_mock_scene_name_has_path_separator(scene))
    {
        return false;
    }
    if (base[0] == 'c')
        ++base;
    baseLen = strlen(base);
    end = base + baseLen;
    if (baseLen >= 4 && strcmp(end - 4, ".sce") == 0)
        end -= 4;
    if (end <= base + 5 ||
        base[0] < '0' || base[0] > '9' ||
        base[1] < '0' || base[1] > '9')
    {
        return false;
    }
    suffix = end - 3;
    if (suffix[0] != '_' ||
        suffix[1] < '0' || suffix[1] > '9' ||
        suffix[2] < '0' || suffix[2] > '9' ||
        suffix <= base + 2)
    {
        return false;
    }
    stemLen = (size_t)(suffix - (base + 2));
    if (3u + stemLen + 2u + 4u + 1u > outCap)
        return false;
    out[0] = base[0];
    out[1] = base[1];
    out[2] = '_';
    memcpy(out + 3, base + 2, stemLen);
    out[3 + stemLen] = suffix[1];
    out[4 + stemLen] = suffix[2];
    memcpy(out + 5 + stemLen, ".sce", 5);
    return strcmp(out, scene) != 0;
}

static bool vm_net_mock_open_server_data_resource(const char *name,
                                                  const char *requiredSuffix,
                                                  FILE **fpOut,
                                                  char *pathOut,
                                                  size_t pathOutCap);

static bool vm_net_mock_client_base_data_resource_exists(
    const char *name, const char *requiredSuffix)
{
    static const char *pathFormats[] = {
        /* The service changes cwd to bin/ before startup validation. */
        "JHOnlineData/%s",
        /* Keep validation usable when called from the project root. */
        "bin/JHOnlineData/%s",
        "../bin/JHOnlineData/%s"
    };
    char candidate[1200];

    if (name == NULL || name[0] == 0 ||
        vm_net_mock_scene_name_has_path_separator(name) ||
        (requiredSuffix != NULL && requiredSuffix[0] != 0 &&
         !vm_net_mock_str_ends_with(name, requiredSuffix)))
    {
        return false;
    }
    for (u32 i = 0; i < sizeof(pathFormats) / sizeof(pathFormats[0]); ++i)
    {
        FILE *fp = NULL;
        snprintf(candidate, sizeof(candidate), pathFormats[i], name);
        fp = vm_net_mock_fopen_game_path(candidate, "rb");
        if (fp == NULL)
            continue;
        fclose(fp);
        return true;
    }
    return false;
}

static bool vm_net_mock_client_data_resource_exists(const char *name,
                                                    const char *requiredSuffix)
{
    char candidate[1200];

    if (vm_net_mock_client_base_data_resource_exists(name, requiredSuffix))
        return true;
    if (name == NULL || name[0] == 0 ||
        vm_net_mock_scene_name_has_path_separator(name) ||
        (requiredSuffix != NULL && requiredSuffix[0] != 0 &&
         !vm_net_mock_str_ends_with(name, requiredSuffix)))
    {
        return false;
    }
    if (g_vm_net_mock_resource_dir[0] != 0 &&
        vm_net_mock_build_configured_resource_path(name, candidate,
                                                   sizeof(candidate)))
    {
        FILE *fp = vm_net_mock_fopen_game_path(candidate, "rb");
        if (fp != NULL)
        {
            fclose(fp);
            return true;
        }
    }
    return false;
}

static bool vm_net_mock_scene_resource_exists(const char *scene)
{
    FILE *fp = NULL;
    char legacyScene[64];

    if (!vm_net_mock_open_server_scene_resource(scene, &fp, NULL, 0))
    {
        if (!vm_net_mock_scene_resource_legacy_alias(scene, legacyScene,
                                                     sizeof(legacyScene)) ||
            !vm_net_mock_open_server_scene_resource(legacyScene, &fp, NULL, 0))
        {
            return false;
        }
    }
    fclose(fp);
    return true;
}

static bool vm_net_mock_parse_equipment_enhance_request(
    const u8 *request,
    u32 requestLen,
    vm_net_mock_equipment_enhance_request *parsedOut)
{
    u32 offset = 4;
    u32 seqValue = 0;
    vm_net_mock_request_object object;
    vm_net_mock_equipment_enhance_request parsed;
    const char *seqField = NULL;
    const u8 *rawValue = NULL;
    u16 rawValueLen = 0;

    if (parsedOut)
        memset(parsedOut, 0, sizeof(*parsedOut));
    memset(&parsed, 0, sizeof(parsed));
    if (request == NULL || requestLen < 9 ||
        request[0] != 'W' || request[1] != 'T' ||
        !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 29 ||
        object.subtype < 1 || object.subtype > 3 || object.payloadLen == 0)
    {
        return false;
    }

    parsed.subtype = object.subtype;
    seqField = parsed.subtype == 1 ? "seq" : "equipseq";
    if (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                             seqField, &seqValue) ||
        seqValue == 0 || seqValue > 0xffffu)
    {
        return false;
    }
    parsed.equipSeq = (u16)seqValue;
    if (parsed.subtype != 1)
    {
        if (!vm_net_mock_get_object_blob_field(
                object.payload, object.payloadLen, "occultinfo",
                &parsed.occultInfo, &parsed.occultInfoLen))
        {
            if (!vm_net_mock_get_object_entry_bytes(
                    object.payload, object.payloadLen, "occultinfo",
                    &rawValue, &rawValueLen))
            {
                return false;
            }
            if (rawValueLen >= 2 &&
                (u16)((((u16)rawValue[0] << 8) | rawValue[1]) + 2u) ==
                    rawValueLen)
            {
                parsed.occultInfo = rawValue + 2;
                parsed.occultInfoLen = (u16)(rawValueLen - 2);
            }
            else
            {
                parsed.occultInfo = rawValue;
                parsed.occultInfoLen = rawValueLen;
            }
        }
        if (parsed.occultInfoLen == 0 ||
            ((parsed.occultInfoLen % 9 != 0 || parsed.occultInfoLen > 45) &&
             (parsed.occultInfoLen % 5 != 0 || parsed.occultInfoLen > 25)))
        {
            return false;
        }
        parsed.materialRows = (u8)(parsed.occultInfoLen /
                                   (parsed.occultInfoLen % 9 == 0 ? 9 : 5));
    }

    if (parsedOut)
        *parsedOut = parsed;
    return true;
}

static bool vm_net_mock_scene_name_is_safe(const char *scene)
{
    if (scene == NULL || scene[0] == 0)
        return false;
    return vm_net_mock_scene_resource_exists(scene);
}

static bool vm_net_mock_read_runtime_scene_name(char *out, size_t outCap)
{
    u32 sceneObj = 0;

    if (out == NULL || outCap == 0)
        return false;
    out[0] = 0;
    if (Global_R9 == 0)
        return false;
    if (uc_mem_read(MTK, Global_R9 + 0x54AC, &sceneObj, sizeof(sceneObj)) != UC_ERR_OK ||
        sceneObj == 0)
    {
        return false;
    }
    return vm_net_read_guest_raw_cstr(sceneObj + 0x475, out, outCap) &&
           vm_net_mock_scene_name_is_safe(out);
}

static const char *vm_net_mock_normalize_scene_name_for_enter(const char *scene)
{
    static char normalized[64];
    if (!vm_net_mock_scene_name_is_safe(scene))
        return vm_net_mock_default_scene_name();

    /*
     * Fresh actorinfo/sceneKey historically used extensionless c-prefixed town
     * keys (`c00..._01`), which the local file layer resolves to `.sce`.
     * Replaying `c00..._NN.sce` directly can be mistaken for a downloadable
     * resource key on re-enter, so strip only that c-prefixed suffix form.
     */
    if (scene[0] == 'c' && vm_net_mock_str_ends_with(scene, ".sce"))
    {
        size_t len = strlen(scene) - 4;
        if (len >= sizeof(normalized))
            len = sizeof(normalized) - 1;
        memcpy(normalized, scene, len);
        normalized[len] = 0;
        return normalized;
    }
    return scene;
}

static void vm_net_mock_copy_normalized_scene_name(const char *scene, char *out, size_t outCap)
{
    if (out == NULL || outCap == 0)
        return;
    out[0] = 0;
    if (scene == NULL || scene[0] == 0)
        return;
    snprintf(out, outCap, "%s", scene);
    {
        size_t len = strlen(out);
        if (len > 4 && strcmp(out + len - 4, ".sce") == 0)
            out[len - 4] = 0;
    }
}

static bool vm_net_mock_scene_names_equal_loose(const char *a, const char *b)
{
    char normalizedA[64];
    char normalizedB[64];

    vm_net_mock_copy_normalized_scene_name(a, normalizedA, sizeof(normalizedA));
    vm_net_mock_copy_normalized_scene_name(b, normalizedB, sizeof(normalizedB));
    return normalizedA[0] != 0 &&
           normalizedB[0] != 0 &&
           strcmp(normalizedA, normalizedB) == 0;
}

static bool vm_net_mock_add_auto_monster_catalog_item(const u8 *scene,
                                                      u32 sceneLen,
                                                      u32 monster1,
                                                      u32 monster2,
                                                      u32 monster3)
{
    vm_net_mock_auto_monster_catalog_item *item = NULL;
    u32 safeLen = 0;

    if (scene == NULL || sceneLen == 0 ||
        (monster1 == 0 && monster2 == 0 && monster3 == 0) ||
        g_vm_net_mock_auto_monster_catalog_count >= VM_NET_MOCK_AUTO_MONSTER_CATALOG_MAX_ITEMS)
    {
        return false;
    }

    safeLen = vm_net_mock_shop_safe_name_len(scene, sceneLen,
                                             sizeof(g_vm_net_mock_auto_monster_catalog[0].scene) - 1);
    if (safeLen == 0)
        return false;

    item = &g_vm_net_mock_auto_monster_catalog[g_vm_net_mock_auto_monster_catalog_count++];
    memset(item, 0, sizeof(*item));
    memcpy(item->scene, scene, safeLen);
    item->scene[safeLen] = 0;
    item->monsterIds[0] = monster1;
    item->monsterIds[1] = monster2;
    item->monsterIds[2] = monster3;
    return true;
}

