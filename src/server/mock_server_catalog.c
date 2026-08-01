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

static u8 vm_net_mock_backpack_client_capacity(u32 capacity)
{
    if (capacity > VM_NET_MOCK_BACKPACK_CLIENT_LOGICAL_CAPACITY)
        return VM_NET_MOCK_BACKPACK_CLIENT_LOGICAL_CAPACITY;
    return (u8)capacity;
}

static bool vm_net_mock_backpack_item_is_client_grid_item(
    const vm_net_mock_backpack_item_state *item)
{
    if (item == NULL || item->itemId == 0 || item->count == 0)
        return false;

    /*
     * mmShopMstarWqvga.cbm:sub_9DE intentionally does not insert these
     * direct-store values into the generic main-item manager after purchase.
     * In particular, item.dsh gives 808 a zero stack limit; the generic
     * TimerControl path normalizes that to one and expands its stored amount
     * into one physical record per unit.  They must therefore not be sent in
     * 30/21 or 17/1 as ordinary backpack rows.
     */
    switch (item->itemId)
    {
    case 808:
    case 817:
    case 818:
    case 819:
        return false;
    default:
        return true;
    }
}

static u8 vm_net_mock_role_backpack_client_grid_count(
    const vm_net_mock_role_state *role)
{
    u8 itemCount = vm_net_mock_role_backpack_count(role);
    u8 gridCount = 0;

    for (u32 i = 0; i < itemCount; ++i)
    {
        if (vm_net_mock_backpack_item_is_client_grid_item(
                &role->backpackItems[i]))
        {
            ++gridCount;
        }
    }
    return gridCount;
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

static u8 vm_net_mock_item_common_extra_stack_byte(u32 itemId, u32 count);
static u8 vm_net_mock_backpack_stack_byte(const vm_net_mock_backpack_item_state *item);
static u16 vm_net_mock_equipment_durability_max_for_item(u32 itemId);

/*
 * Unequipped bag equipment has no per-row durability table.  mmGame:sub_D04 /
 * JianghuOL.CBE item+0x110 treat the wire u32 count as current durability for
 * equip ids, so backpack grants and grid rows must send equip.dsh 耐久 (max),
 * never the instance count of 1.
 */
static u32 vm_net_mock_backpack_equipment_wire_count(u32 itemId)
{
    return vm_net_mock_equipment_durability_max_for_item(itemId);
}

static u32 vm_net_mock_backpack_grid_wire_count(const vm_net_mock_backpack_item_state *item)
{
    if (item == NULL)
        return 0;
    /*
     * JianghuOL.CBE:0x01039952 → 0x0101918E stores this u32 (truncated to u16)
     * in item+0xf2 (+242) as the visible stack quantity, and may ADD it into
     * a tip/occupancy accumulator.  Equipment must stay instance count 1 here.
     *
     * Current durability for equip ids is NOT this field on the login grid
     * path; worn-slot / 7/7 (mmGame:0xD04) paths carry durability separately.
     * Sending equip.dsh 耐久 max (e.g. 50) as grid count made one enhanced
     * piece fill the backpack after login.
     */
    if (vm_net_mock_backpack_item_id_uses_reservoir_count(item->itemId))
        return item->count == 0 ? 0 : 1;
    return item->count;
}

typedef struct
{
    u32 itemId;
    char name[VM_NET_MOCK_SHOP_NAME_BYTES + 1];
    char description[VM_NET_MOCK_SHOP_DESC_BYTES + 1];
    u32 price;
    u32 stock;
    u8 stack;
    u8 visual;
    u8 isEquip;
    u8 category;
    u8 enabled;
    /* 1 once a server_shop_items row was applied (price/enabled override). */
    u8 shopDbListed;
} vm_net_mock_shop_catalog_item;

typedef struct
{
    u32 itemId;
    u8 slot;
    u8 levelRequired;
    /* equip.dsh column 6 (`品质`): 0=白装, higher values are colored gear. */
    u8 quality;
    /* equip.dsh column 19 (`耐久`) is the client-visible maximum.  The
     * wire payload only carries the current value, so the server must use this
     * same source whenever it creates or repairs durable equipment. */
    u16 durabilityMax;
    vm_net_mock_equipment_bonus bonus;
} vm_net_mock_equipment_catalog_item;

typedef struct
{
    u32 itemId;
    u8 category;
    u8 levelRequired;
    u8 stack;
    u8 consumeMode;
    u8 durationMinutes;
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
    /* skill.dsh `目标指向`: 2=friendly group, 3=one enemy, 4=enemy group. */
    u8 targetDirection;
    u8 durationRounds;
    /* Timed battle modifiers from skill.dsh columns 16..24.  They are kept
     * signed because defensive spell rows may trade one attribute for another. */
    int32_t strengthChange;
    int32_t agilityChange;
    int32_t wisdomChange;
    int32_t attackChange;
    int32_t defenseChange;
    int32_t critChange;
    int32_t hitChange;
    int32_t dodgeChange;
    int32_t resistChange;
    /* skill.dsh `效果`: 0 normal, 1 silence, 2 dispel, 3 revive/summon. */
    u8 effectKind;
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
static u16 vm_net_mock_equipment_durability_max_for_item(u32 itemId);

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
                                               u32 targetDirection,
                                               u32 durationRounds,
                                               int32_t strengthChange,
                                               int32_t agilityChange,
                                               int32_t wisdomChange,
                                               int32_t attackChange,
                                               int32_t defenseChange,
                                               int32_t critChange,
                                               int32_t hitChange,
                                               int32_t dodgeChange,
                                               int32_t resistChange,
                                               u32 effectKind,
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
    skill->targetDirection = (u8)(targetDirection > 255 ? 255 : targetDirection);
    skill->durationRounds = (u8)(durationRounds > 255 ? 255 : durationRounds);
    skill->strengthChange = strengthChange;
    skill->agilityChange = agilityChange;
    skill->wisdomChange = wisdomChange;
    skill->attackChange = attackChange;
    skill->defenseChange = defenseChange;
    skill->critChange = critChange;
    skill->hitChange = hitChange;
    skill->dodgeChange = dodgeChange;
    skill->resistChange = resistChange;
    skill->effectKind = (u8)(effectKind > 255 ? 255 : effectKind);
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
        u32 targetDirection = 0;
        u32 durationRounds = 0;
        int32_t strengthChange = 0;
        int32_t agilityChange = 0;
        int32_t wisdomChange = 0;
        int32_t attackChange = 0;
        int32_t defenseChange = 0;
        int32_t critChange = 0;
        int32_t hitChange = 0;
        int32_t dodgeChange = 0;
        int32_t resistChange = 0;
        u32 effectKind = 0;
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
            case 10:
                /* `目标指向`: authoritative battle target scope. */
                targetDirection = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                break;
            case 9:
                durationRounds = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
                break;
            case 14:
                hpChange = vm_net_mock_parse_dsh_s32(value, valueLen, 0);
                break;
            case 16:
                strengthChange = vm_net_mock_parse_dsh_s32(value, valueLen, 0);
                break;
            case 17:
                agilityChange = vm_net_mock_parse_dsh_s32(value, valueLen, 0);
                break;
            case 18:
                wisdomChange = vm_net_mock_parse_dsh_s32(value, valueLen, 0);
                break;
            case 19:
                attackChange = vm_net_mock_parse_dsh_s32(value, valueLen, 0);
                break;
            case 20:
                defenseChange = vm_net_mock_parse_dsh_s32(value, valueLen, 0);
                break;
            case 21:
                critChange = vm_net_mock_parse_dsh_s32(value, valueLen, 0);
                break;
            case 22:
                hitChange = vm_net_mock_parse_dsh_s32(value, valueLen, 0);
                break;
            case 23:
                dodgeChange = vm_net_mock_parse_dsh_s32(value, valueLen, 0);
                break;
            case 24:
                resistChange = vm_net_mock_parse_dsh_s32(value, valueLen, 0);
                break;
            case 25:
                /* `效果`: 1 silence, 2 dispel, 3 revive (尸鬼召唤). */
                effectKind = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
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
                                               targetDirection,
                                               durationRounds,
                                               strengthChange,
                                               agilityChange,
                                               wisdomChange,
                                               attackChange,
                                               defenseChange,
                                               critChange,
                                               hitChange,
                                               dodgeChange,
                                               resistChange,
                                               effectKind,
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
                                                -130, 50, 0, 0, 3, 0,
                                                0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                (const u8 *)"\xcd\xf2\xbd\xa3\xd6\xef\xcf\xc9\x31",
                                                9);
        (void)vm_net_mock_add_skill_catalog_item(101, 1, 1, 1, 50, 20,
                                                -75, 0, 50, 0, 3, 0,
                                                0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                (const u8 *)"\xb7\xe7\xce\xe8\xc8\xd0\xd0\xd0\x31",
                                                9);
        (void)vm_net_mock_add_skill_catalog_item(201, 2, 1, 7, 50, 5,
                                                -30, 0, 0, 110, 3, 0,
                                                0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
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
            "durability SMALLINT UNSIGNED NOT NULL,"
            "durability_max SMALLINT UNSIGNED NOT NULL,"
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
        u32 itemId = role->equippedItemIds[slot];
        u16 durabilityMax = vm_net_mock_equipment_durability_max_for_item(itemId);
        bool itemChanged = state->equipmentItemIds[slot] != itemId;
        bool maximumChanged = state->durabilityMax[slot] != durabilityMax;

        /* Empty slots have no durability state. */
        if (itemId == 0)
        {
            if (itemChanged)
            {
                state->equipmentItemIds[slot] = 0;
                state->durability[slot] = 0;
                /* The schema predates empty-slot durability and requires a
                 * positive max.  Retain the former item's valid max; the
                 * zero item id makes the row non-durable to every consumer. */
                if (state->durabilityMax[slot] == 0)
                    state->durabilityMax[slot] = 1;
                if (!vm_net_mock_role_service_persist_durability(state, slot))
                {
                    printf("[error][network] mock_role_durability_sync_store role=%u slot=%u item=0 error=%s\n",
                           state->roleId, slot, vm_mysql_last_error());
                }
            }
            continue;
        }
        /* Do not replace persisted data with a fabricated max if the exact
         * local equip.dsh record is unavailable.  Non-zero equipment is only
         * usable after this catalog lookup succeeds. */
        if (durabilityMax == 0)
        {
            printf("[warn][network] mock_role_durability_sync_unresolved role=%u slot=%u item=%u source=equip.dsh\n",
                   state->roleId, slot, itemId);
            continue;
        }
        if (!itemChanged && !maximumChanged &&
            state->durability[slot] <= durabilityMax)
        {
            continue;
        }

        /* A new item starts full.  For a legacy row for the same item, retain
         * its current value only while it is valid under equip.dsh; otherwise
         * clamp it to the real max before any quote or repair is calculated. */
        state->equipmentItemIds[slot] = itemId;
        state->durabilityMax[slot] = durabilityMax;
        if (itemChanged)
            state->durability[slot] = durabilityMax;
        else if (state->durability[slot] > durabilityMax)
            state->durability[slot] = durabilityMax;
        if (!vm_net_mock_role_service_persist_durability(state, slot))
        {
            printf("[error][network] mock_role_durability_sync_store role=%u slot=%u item=%u durability=%u max=%u error=%s\n",
                   state->roleId, slot, itemId, state->durability[slot],
                   state->durabilityMax[slot], vm_mysql_last_error());
        }
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
    vm_net_mock_role_mark_inventory_dirty("npc-equipment-repair");
    return true;
}

/* Restore equipped durability without copper; used by 7/29 quick repair (酷宝). */
static bool vm_net_mock_role_service_repair_all_free(vm_net_mock_role_state *role,
                                                     u16 *repairCountOut)
{
    vm_net_mock_role_service_state *state =
        vm_net_mock_role_service_state_get(role);
    u16 count = 0;

    if (repairCountOut)
        *repairCountOut = 0;
    if (state == NULL || role == NULL)
        return false;
    vm_net_mock_role_service_sync_equipment(state, role);
    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        if (role->equippedItemIds[slot] == 0 ||
            state->durability[slot] >= state->durabilityMax[slot])
        {
            continue;
        }
        state->durability[slot] = state->durabilityMax[slot];
        (void)vm_net_mock_role_service_persist_durability(state, slot);
        ++count;
    }
    if (repairCountOut)
        *repairCountOut = count;
    if (count != 0)
        vm_net_mock_role_mark_inventory_dirty("quick-equipment-repair");
    return true;
}

static bool vm_net_mock_role_service_repair_one_free(vm_net_mock_role_state *role,
                                                     u16 equipSeq,
                                                     u32 itemId,
                                                     u16 *repairCountOut)
{
    vm_net_mock_role_service_state *state =
        vm_net_mock_role_service_state_get(role);
    u32 slot;

    if (repairCountOut)
        *repairCountOut = 0;
    if (state == NULL || role == NULL || equipSeq == 0 ||
        equipSeq > VM_NET_MOCK_EQUIP_SLOT_COUNT)
    {
        return false;
    }
    slot = (u32)equipSeq - 1u;
    vm_net_mock_role_service_sync_equipment(state, role);
    if (role->equippedItemIds[slot] == 0)
        return false;
    if (itemId != 0 && role->equippedItemIds[slot] != itemId)
        return false;
    if (state->durability[slot] >= state->durabilityMax[slot])
        return true;
    state->durability[slot] = state->durabilityMax[slot];
    (void)vm_net_mock_role_service_persist_durability(state, slot);
    if (repairCountOut)
        *repairCountOut = 1;
    vm_net_mock_role_mark_inventory_dirty("quick-equipment-repair-one");
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
    /*
     * item.dsh 828: 战斗心得 includes 「自动修装备」.  While the timed
     * insight effect is active, skip the per-battle durability decrement so
     * worn gear stays at its current values without an NPC repair visit.
     */
    if (vm_net_mock_role_active_battle_exp_bonus_percent(role) != 0)
    {
        printf("[info][network] mock_equipment_durability_wear_skip role=%u "
               "battle=%u reason=battle-insight-auto-repair "
               "evidence=item.dsh:828\n",
               role->roleId, g_mockBattleOperateSessionSerial);
        return;
    }
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
                                              const u8 *description, u32 descriptionLen,
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
    if (description != NULL && descriptionLen != 0)
    {
        copyLen = vm_net_mock_shop_safe_name_len(
            description, descriptionLen, VM_NET_MOCK_SHOP_DESC_BYTES);
        memcpy(item->description, description, copyLen);
        item->description[copyLen] = 0;
    }
    item->price = price ? price : VM_NET_MOCK_SHOP_DEFAULT_ITEM_PRICE;
    item->stock = stock ? stock : VM_NET_MOCK_SHOP_DEFAULT_ITEM_STOCK;
    item->stack = stack ? stack : 1;
    item->visual = visual ? visual : 1;
    item->isEquip = equip ? 1 : 0;
    item->category = (u8)(category > 255 ? 255 : category);
    item->enabled = 1;
    item->shopDbListed = 0;
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
        const u8 *description = NULL;
        u32 descriptionLen = 0;

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
            else if ((!equip && col == 4) || (equip && col == 2))
            {
                description = value;
                descriptionLen = valueLen;
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
         * item.dsh "价值" matches ordinary item/equipment copper values, but the
         * mall secret page (`14/5`) sells for W-coin. Prefer the dedicated
         * "酷宝" column whenever it is set. This covers native `类别=14` rows
         * and the category-10 premium rows that are appended when shelved
         * (802/803/809/828/…); without it those rows keep copper `价值`
         * (often 0 or millions) and become unpurchasable or mispriced.
         */
        if (!equip && kubaoPrice != 0)
            price = kubaoPrice;

        if (vm_net_mock_add_shop_catalog_item(itemId,
                                             name,
                                             nameLen,
                                             description,
                                             descriptionLen,
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
                                                NULL,
                                                0,
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

/*
 * Drop-row / MySQL callbacks must never trigger a nested COM_QUERY on the
 * same persistent connection.  Only inspect an already-materialized catalog.
 */
static bool vm_net_mock_shop_catalog_has_loaded_item(u32 itemId)
{
    if (!g_vm_net_mock_shop_catalog_loaded || itemId == 0)
        return false;
    for (u32 i = 0; i < g_vm_net_mock_shop_catalog_count; ++i)
    {
        if (g_vm_net_mock_shop_catalog[i].itemId == itemId)
            return true;
    }
    return false;
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
    item->shopDbListed = 1;
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
    item->shopDbListed = 1;
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
                                                   u32 quality, u32 category,
                                                   u32 durabilityMax,
                                                   const vm_net_mock_equipment_bonus *bonus)
{
    vm_net_mock_equipment_catalog_item *item = NULL;
    u8 slot = vm_net_mock_equipment_slot_for_category(category);

    if (itemId == 0 || durabilityMax == 0 || durabilityMax > 0xffffu ||
        bonus == NULL || slot >= VM_NET_MOCK_EQUIP_SLOT_COUNT ||
        g_vm_net_mock_equipment_catalog_count >= VM_NET_MOCK_EQUIP_CATALOG_MAX_ITEMS)
    {
        return false;
    }

    item = &g_vm_net_mock_equipment_catalog[g_vm_net_mock_equipment_catalog_count++];
    memset(item, 0, sizeof(*item));
    item->itemId = itemId;
    item->slot = slot;
    item->levelRequired = (u8)(levelRequired > 255 ? 255 : levelRequired);
    item->quality = (u8)(quality > 255 ? 255 : quality);
    item->durabilityMax = (u16)durabilityMax;
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
        u32 quality = 0;
        u32 category = 0xffffffffu;
        u32 durabilityMax = 0;
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
            case 6:
                quality = parsed;
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
            case 19:
                durabilityMax = parsed;
                break;
            default:
                break;
            }
            rowPos += valueLen;
        }

        if (vm_net_mock_add_equipment_catalog_item(itemId, levelRequired, quality,
                                                   category, durabilityMax, &bonus))
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

/*
 * ParseEquipAttributes first i16 for non-equipment is the visible stack byte.
 * Equipment must NOT put stack=1 here: the client maps the first i16 low byte
 * onto item+0xe used by backpack name 「(+N)」 (warehouse (1,0) → (+1) bug).
 * For equipment the first i16 is the current enhance level; callers still pass
 * stackRuntimeByte=0 and the real level via enhanceLevel — see
 * vm_net_mock_seq_put_item_common_extra.
 */
static u8 vm_net_mock_item_common_extra_stack_byte(u32 itemId, u32 count)
{
    if (itemId == 0 || count == 0)
        return 0;
    if (vm_net_mock_find_equipment_catalog_item(itemId) != NULL)
        return 0;
    if (vm_net_mock_backpack_item_id_uses_reservoir_count(itemId))
        return 1;
    return count > 255 ? 255 : (u8)count;
}

static u8 vm_net_mock_backpack_stack_byte(const vm_net_mock_backpack_item_state *item)
{
    if (item == NULL)
        return 0;
    return vm_net_mock_item_common_extra_stack_byte(item->itemId, item->count);
}

static u16 vm_net_mock_equipment_durability_max_for_item(u32 itemId)
{
    const vm_net_mock_equipment_catalog_item *item =
        vm_net_mock_find_equipment_catalog_item(itemId);

    return item != NULL ? item->durabilityMax : 0;
}

static bool vm_net_mock_add_item_effect_catalog_item(u32 itemId, u32 category,
                                                     u32 levelRequired, u32 stack,
                                                     u32 consumeMode, u32 durationMinutes,
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
    item->durationMinutes = (u8)(durationMinutes > 255 ? 255 : durationMinutes);
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
        u32 durationMinutes = 0;
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
            case 13:
                durationMinutes = parsed;
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
                                                     stack, consumeMode, durationMinutes,
                                                     hp, mp, exp))
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
        (void)vm_net_mock_add_item_effect_catalog_item(301, 10, 1, 20, 1, 0, 100, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(302, 10, 1, 20, 1, 0, 350, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(303, 10, 1, 20, 1, 0, 600, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(304, 10, 1, 20, 1, 0, 850, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(305, 10, 1, 20, 1, 0, 1100, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(321, 10, 1, 20, 1, 0, 0, 100, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(322, 10, 1, 20, 1, 0, 0, 350, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(323, 10, 1, 20, 1, 0, 0, 600, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(324, 10, 1, 20, 1, 0, 0, 850, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(325, 10, 1, 20, 1, 0, 0, 1100, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(802, 10, 200, 1, 2, 0, 50000, 0, 0);
        (void)vm_net_mock_add_item_effect_catalog_item(803, 10, 200, 1, 2, 0, 0, 50000, 0);
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

/* These ids have client-side request/response handlers that are distinct from
 * the ordinary 7/1 consumable flow.  Keeping the classification here prevents
 * a future generic caller from silently deleting a special item just because
 * it happens to be in category 10. */
static bool vm_net_mock_item_requires_special_use_protocol(u32 itemId)
{
    switch (itemId)
    {
    case 801: /* revival stone: battle 1/7/14 or map shop-return consume only */
    case 522: /* bronze/silver/gold chests: 1/7/15 + matching key */
    case 523:
    case 524:
    case 809:
    case 810:
    case 811:
    case 845: /* 三十倍经验卡（自定义，克隆自 811） */
    case 813: /* chest keys must not fall into generic 7/1 category-10 consume */
    case 814:
    case 815:
    case 827:
    case 828:
    case 829:
    case 830:
    case 833:
    case 920:
    case 921:
        return true;
    default:
        return false;
    }
}

static bool vm_net_mock_item_effect_is_usable(const vm_net_mock_item_effect_catalog_item *item)
{
    if (item == NULL)
        return false;
    if (vm_net_mock_item_requires_special_use_protocol(item->itemId))
        return false;
    return item->category == 10 || item->hp != 0 || item->mp != 0 || item->exp != 0;
}

static bool vm_net_mock_item_effect_is_reservoir(
    const vm_net_mock_item_effect_catalog_item *item)
{
    if (item == NULL || (item->hp == 0 && item->mp == 0))
        return false;
    /*
     * item.dsh marks 802/803 with consumeMode=0, but mmGame:0xD04 still
     * treats their backpack u32 count as the HP/MP reservoir (visible stack
     * forced to 1).  Trust the hard-coded flask ids when DSH omits mode=2;
     * otherwise mall/NPC add_backpack falls through to itemId stack-merge
     * and a second flask never gets its own seq.
     */
    if (vm_net_mock_backpack_item_id_uses_reservoir_count(item->itemId))
        return true;
    return item->consumeMode == 2;
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
    u32 capacity = vm_net_mock_backpack_client_capacity(
        vm_net_mock_env_u8("CBE_ACTOR_BACKPACK_CAPACITY",
                           role ? role->backpackCapacity :
                           VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY));

    /*
     * mmShopMstarWqvga.cbm:0x7EC maps the local capacity halfword:
     *   12 -> price 20, 16 -> price 40, else -> price 60.
     * Initial roles start at 20, so mall rows normally show 60.
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
    case 5:
        /*
         * 秘宝道具 (14/5): native category 14, shelved category-10 meds, and
         * any other non-equip row with server_shop_items.enabled=1 so admin
         * shelf overrides are actually purchasable in the mall.
         */
        return !item->isEquip &&
               (item->category == 14 || item->category == 10 ||
                item->shopDbListed != 0);
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

/*
 * JianghuOL.CBE name table at 0x01003B42 (1-based wire attr types):
 * 1力量 2敏捷 3智慧 4物攻 5护甲 6躲闪 7命中 8暴击 9气血 10法力.
 * Display 0x01032118: flag!=0 appends '%' (气血%/法力%).
 * There is no wire type for 「抗性」 in this table (resist = actorinfo
 * word[5]→+0x12c).
 *
 * Wear-apply jump 0x010100CE indexes by wire type:
 *   0→+0x122 1→+0x124 2→+0x130 3→+0x132 4→+0x128 5→+0x12a 6→+0x12e
 *   7/9→HP pool  8/10→MP pool.  No jump writes +0x12c.
 *
 * Login panel evidence (role34): paint column is
 * 力/敏/物攻/护甲/闪躲/命中/暴击/抗性 at those offsets; word[2]=命中,
 * word[5]=抗性. Remap enhance wire so milestones land correctly; detail
 * names then follow the *wire* byte name (unresolved vs official).
 * 命中 stays wire 7 (detail name) — panel value comes from actorinfo.
 *
 * Each equip slot has 4 milestone lines at +4/+8/+12/+16.
 * Entries marked [P] are provisional balance fill (replace when confirmed).
 * Confirmed player evidence is unmarked.
 *
 * After milestones (or alone when 1<=L<4), wire also appends M(L) deltas for
 * 护甲/物攻/气血/法力 when that column's equip.dsh base is > 0.  Client item
 * attr arrays only hold 6 entries (ParseEquipAttributes cmp #6), so total
 * attr_count is capped at CLIENT_CAP — milestones take priority, then extras
 * in 护甲→物攻→气血→法力 order.
 *
 * Balance intent: weapon is shared by all three jobs — prefer universal
 * lines (暴击/躲闪/护甲/气血), not 物攻/力量.  Job-leaning offense
 * (物攻/力量) lives on pants/ring/belt instead.
 */
enum
{
    VM_NET_MOCK_EQUIP_ATTR_STRENGTH = 1,
    VM_NET_MOCK_EQUIP_ATTR_AGILITY = 2,
    VM_NET_MOCK_EQUIP_ATTR_WISDOM = 3,
    VM_NET_MOCK_EQUIP_ATTR_ATTACK = 4,
    VM_NET_MOCK_EQUIP_ATTR_ARMOR = 5,
    VM_NET_MOCK_EQUIP_ATTR_DODGE = 6,
    VM_NET_MOCK_EQUIP_ATTR_HIT = 7,
    VM_NET_MOCK_EQUIP_ATTR_CRIT = 8,
    VM_NET_MOCK_EQUIP_ATTR_HP = 9,
    VM_NET_MOCK_EQUIP_ATTR_MP = 10,
    VM_NET_MOCK_EQUIP_ATTR_MILESTONE_SLOTS = 4,
    VM_NET_MOCK_EQUIP_ATTR_CLIENT_CAP = 6,
    /* legacy alias used by milestone count / older call sites */
    VM_NET_MOCK_EQUIP_ATTR_MAX_SLOTS = VM_NET_MOCK_EQUIP_ATTR_MILESTONE_SLOTS
};

/*
 * Detail UI 0x01032118: "(+%d)%s+%d" uses unlock + 1-based name table
 * (1力量 2敏捷 3智慧 4物攻 5护甲 6躲闪 7命中 8暴击 9气血 10法力).
 *
 * Wear-apply jump 0x010100D0 is a DIFFERENT 0-based map
 * (0力 1敏 2物攻 3护甲 4躲 5命 6暴). Emitting jump indices made
 * 护甲→type3 paint as 智慧、法力%→type8 paint as 暴击, and unlock=255
 * paint as (+255). Official detail contract uses name-table types.
 *
 * Jump apply with name types lands on the wrong actor halfwords; panel
 * authority stays actorinfo bare + fec6 dsh + F8/FA. Milestone flats for
 * battle/server stats remain in collect_equipment_bonus.
 *
 * After milestones (and when 1<=L<4 with no milestone block), also emit
 * M(L)-base 护甲/物攻 lines when dsh base>0 so the player can see how much
 * enhance added. unlock=1 (not 255 — that painted as (+255)). Real armor/
 * attack numbers still come from F8 / weapon+0xFA scale, not these lines.
 */
static u8 vm_net_mock_equip_enhance_wire_type(u8 semanticType)
{
    return semanticType; /* name-table type for detail (+N)名称 */
}

/* Keep real 4/8/12/16 so detail shows (+4) not (+255). */
static u8 vm_net_mock_equip_enhance_wire_unlock(u8 semanticType, u8 realUnlock)
{
    (void)semanticType;
    return realUnlock;
}

typedef struct
{
    u8 type;
    u8 flag; /* 0=flat 值, 1=percent % */
} vm_net_mock_equip_enhance_attr_def;

/* slot 0 武器 — 三职业共用：躲闪 + 护甲(抗性向，无独立抗性 type) */
static const vm_net_mock_equip_enhance_attr_def
    g_vm_net_mock_weapon_enhance_attrs[] = {
        {VM_NET_MOCK_EQUIP_ATTR_CRIT, 0},  /* +4  暴击 */
        {VM_NET_MOCK_EQUIP_ATTR_DODGE, 0}, /* +8  躲闪 [P] */
        {VM_NET_MOCK_EQUIP_ATTR_ARMOR, 0}, /* +12 护甲 [P]（抗性向） */
        {VM_NET_MOCK_EQUIP_ATTR_HIT, 0},   /* +16 命中 [P] */
};

/* slot 1 帽子 — 三围（智慧）固定 +16 */
static const vm_net_mock_equip_enhance_attr_def
    g_vm_net_mock_helm_enhance_attrs[] = {
        {VM_NET_MOCK_EQUIP_ATTR_ARMOR, 0},  /* +4  护甲 [P] */
        {VM_NET_MOCK_EQUIP_ATTR_HP, 0},     /* +8  气血 [P] */
        {VM_NET_MOCK_EQUIP_ATTR_MP, 1},     /* +12 法力% [P] */
        {VM_NET_MOCK_EQUIP_ATTR_WISDOM, 0}, /* +16 智慧 [P] */
};

/* slot 2 上衣 */
static const vm_net_mock_equip_enhance_attr_def
    g_vm_net_mock_chest_enhance_attrs[] = {
        {VM_NET_MOCK_EQUIP_ATTR_CRIT, 0},     /* +4  暴击 */
        {VM_NET_MOCK_EQUIP_ATTR_HP, 1},       /* +8  气血% */
        {VM_NET_MOCK_EQUIP_ATTR_MP, 1},       /* +12 法力% */
        {VM_NET_MOCK_EQUIP_ATTR_STRENGTH, 0}, /* +16 力量 [P] */
};

/* slot 3 披风 */
static const vm_net_mock_equip_enhance_attr_def
    g_vm_net_mock_cloak_enhance_attrs[] = {
        {VM_NET_MOCK_EQUIP_ATTR_CRIT, 0},    /* +4  暴击 */
        {VM_NET_MOCK_EQUIP_ATTR_HIT, 0},     /* +8  命中 */
        {VM_NET_MOCK_EQUIP_ATTR_DODGE, 0},   /* +12 躲闪 [P] */
        {VM_NET_MOCK_EQUIP_ATTR_AGILITY, 0}, /* +16 敏捷 [P] */
};

/* slot 4 束腰 — 三围（力量）固定 +16；气血与法力各一档 */
static const vm_net_mock_equip_enhance_attr_def
    g_vm_net_mock_belt_enhance_attrs[] = {
        {VM_NET_MOCK_EQUIP_ATTR_ARMOR, 0},    /* +4  护甲 [P] */
        {VM_NET_MOCK_EQUIP_ATTR_MP, 0},       /* +8  法力 [P] */
        {VM_NET_MOCK_EQUIP_ATTR_HP, 1},       /* +12 气血% [P] */
        {VM_NET_MOCK_EQUIP_ATTR_STRENGTH, 0}, /* +16 力量 [P] */
};

/* slot 5 裤子 — 承接武器挪出的物攻（+4 已确认） */
static const vm_net_mock_equip_enhance_attr_def
    g_vm_net_mock_legs_enhance_attrs[] = {
        {VM_NET_MOCK_EQUIP_ATTR_ATTACK, 0}, /* +4  物攻 */
        {VM_NET_MOCK_EQUIP_ATTR_ARMOR, 0},  /* +8  护甲 */
        {VM_NET_MOCK_EQUIP_ATTR_HIT, 0},    /* +12 命中 [P]（原武器命中） */
        {VM_NET_MOCK_EQUIP_ATTR_HP, 0},     /* +16 气血 [P] */
};

/* slot 6 鞋子 — 三围（敏捷）固定 +16 */
static const vm_net_mock_equip_enhance_attr_def
    g_vm_net_mock_boots_enhance_attrs[] = {
        {VM_NET_MOCK_EQUIP_ATTR_DODGE, 0},   /* +4  躲闪 [P] */
        {VM_NET_MOCK_EQUIP_ATTR_HP, 0},      /* +8  气血 [P] */
        {VM_NET_MOCK_EQUIP_ATTR_HIT, 0},     /* +12 命中 [P] */
        {VM_NET_MOCK_EQUIP_ATTR_AGILITY, 0}, /* +16 敏捷 [P] */
};

/* slot 7 饰品 — 物攻保留；护甲与上衣互换 */
static const vm_net_mock_equip_enhance_attr_def
    g_vm_net_mock_ring_enhance_attrs[] = {
        {VM_NET_MOCK_EQUIP_ATTR_ATTACK, 0}, /* +4  物攻 */
        {VM_NET_MOCK_EQUIP_ATTR_HP, 0},     /* +8  气血 */
        {VM_NET_MOCK_EQUIP_ATTR_ARMOR, 0},  /* +12 护甲 [P]（与上衣互换） */
        {VM_NET_MOCK_EQUIP_ATTR_WISDOM, 0}, /* +16 智慧 [P] */
};

/*
 * Equip.dsh columns with base > 0 scale by enhance level:
 *   M(L) = 1 + (10*L + 35*floor(L/4)) / 100
 * Regular step +10% of base; milestones +4/+8/+12/+16 add an extra +35%
 * (that step is +45%).  +16 → exactly 4× base.  Base 0 stays 0.
 *
 * Shared by role collect_equipment_bonus and shop/sell detail text.
 * 强化基础缩放（M(L)）仅护甲/物攻；气血/法力不参与。
 * 强化附加 UI: +4/+8/+12/+16 milestones, then M(L) deltas for
 * 护甲/物攻 with base>0 (client cap 6).
 */
static u32 vm_net_mock_equipment_bonus_scale(u32 base, u16 enhanceLevel)
{
    u32 level = enhanceLevel;
    u32 tiers;
    u32 bonusPct;

    if (base == 0 || level == 0)
        return base;
    if (level > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
        level = VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL;
    tiers = level / 4u;
    bonusPct = 10u * level + 35u * tiers;
    return base + (base * bonusPct) / 100u;
}

static u32 vm_net_mock_equipment_bonus_enhance_delta(u32 base, u16 enhanceLevel)
{
    u32 scaled = vm_net_mock_equipment_bonus_scale(base, enhanceLevel);

    return scaled > base ? scaled - base : 0;
}

/*
 * Milestone (+4/+8/+12/+16) wire values by equip.dsh 品质.
 * 品质 0=白装；表格式档位按 1..4（白装回退到档 1）。
 * 暴击/闪躲/命中: quality+1
 * 气血%/法力% (flag!=0): (quality+1)*5
 * 力量/敏捷/智慧: (quality+1)*120（三围词条固定在 +16）
 * 气血/法力 值: 150/300/450/600
 * 物攻: 23/50/123/296
 * 护甲: 175/350/525/700
 */
static u8 vm_net_mock_equip_quality_table_tier(u8 quality)
{
    if (quality < 1)
        return 1;
    if (quality > 4)
        return 4;
    return quality;
}

static const vm_net_mock_equip_enhance_attr_def *
vm_net_mock_equip_enhance_attrs_for_slot(u8 slot, u8 *countOut);

static u16 vm_net_mock_equip_enhance_attr_value_for_type(u8 quality, u8 type,
                                                         u8 flag)
{
    static const u16 kHpMpFlatByQuality[5] = {0, 150, 300, 450, 600};
    static const u16 kAttackByQuality[5] = {0, 23, 50, 123, 296};
    static const u16 kArmorByQuality[5] = {0, 175, 350, 525, 700};
    u8 tier = vm_net_mock_equip_quality_table_tier(quality);
    u32 qualityPlus = (u32)quality + 1u;

    switch (type)
    {
    case VM_NET_MOCK_EQUIP_ATTR_CRIT:
    case VM_NET_MOCK_EQUIP_ATTR_DODGE:
    case VM_NET_MOCK_EQUIP_ATTR_HIT:
        return (u16)qualityPlus;
    case VM_NET_MOCK_EQUIP_ATTR_STRENGTH:
    case VM_NET_MOCK_EQUIP_ATTR_AGILITY:
    case VM_NET_MOCK_EQUIP_ATTR_WISDOM:
        return (u16)(qualityPlus * 120u);
    case VM_NET_MOCK_EQUIP_ATTR_HP:
    case VM_NET_MOCK_EQUIP_ATTR_MP:
        if (flag != 0)
            return (u16)(qualityPlus * 5u);
        return kHpMpFlatByQuality[tier];
    case VM_NET_MOCK_EQUIP_ATTR_ATTACK:
        return kAttackByQuality[tier];
    case VM_NET_MOCK_EQUIP_ATTR_ARMOR:
        return kArmorByQuality[tier];
    default:
        return 1;
    }
}

/* Add unlocked +4/+8/+12/+16 milestone flats (quality table) into dst. */
static void vm_net_mock_equipment_bonus_add_unlocked_milestones(
    vm_net_mock_equipment_bonus *dst,
    const vm_net_mock_equipment_catalog_item *item,
    u16 enhanceLevel)
{
    const vm_net_mock_equip_enhance_attr_def *defs = NULL;
    u8 knownCount = 0;
    u8 i = 0;

    if (dst == NULL || item == NULL || enhanceLevel < 4)
        return;
    defs = vm_net_mock_equip_enhance_attrs_for_slot(item->slot, &knownCount);
    if (defs == NULL || knownCount == 0)
        return;
    if (knownCount > VM_NET_MOCK_EQUIP_ATTR_MILESTONE_SLOTS)
        knownCount = VM_NET_MOCK_EQUIP_ATTR_MILESTONE_SLOTS;
    for (i = 0; i < knownCount; ++i)
    {
        u8 unlock = (u8)((i + 1u) * 4u);
        u16 value;
        u32 add = 0;

        if (unlock > enhanceLevel)
            continue;
        value = vm_net_mock_equip_enhance_attr_value_for_type(
            item->quality, defs[i].type, defs[i].flag);
        switch (defs[i].type)
        {
        case VM_NET_MOCK_EQUIP_ATTR_STRENGTH:
            dst->strength += value;
            break;
        case VM_NET_MOCK_EQUIP_ATTR_AGILITY:
            dst->agility += value;
            break;
        case VM_NET_MOCK_EQUIP_ATTR_WISDOM:
            dst->wisdom += value;
            break;
        case VM_NET_MOCK_EQUIP_ATTR_ATTACK:
            dst->attack += value;
            break;
        case VM_NET_MOCK_EQUIP_ATTR_ARMOR:
            dst->armor += value;
            break;
        case VM_NET_MOCK_EQUIP_ATTR_DODGE:
            dst->dodge += value;
            break;
        case VM_NET_MOCK_EQUIP_ATTR_HIT:
            dst->hit += value;
            break;
        case VM_NET_MOCK_EQUIP_ATTR_CRIT:
            dst->crit += value;
            break;
        case VM_NET_MOCK_EQUIP_ATTR_HP:
            if (defs[i].flag != 0)
            {
                add = (item->bonus.hp * (u32)value) / 100u;
                dst->hp += add;
            }
            else
                dst->hp += value;
            break;
        case VM_NET_MOCK_EQUIP_ATTR_MP:
            if (defs[i].flag != 0)
            {
                add = (item->bonus.mp * (u32)value) / 100u;
                dst->mp += add;
            }
            else
                dst->mp += value;
            break;
        default:
            break;
        }
    }
}

static const vm_net_mock_equip_enhance_attr_def *
vm_net_mock_equip_enhance_attrs_for_slot(u8 slot, u8 *countOut)
{
    if (countOut == NULL)
        return NULL;
    switch (slot)
    {
    case 0: /* 武器 */
        *countOut = (u8)(sizeof(g_vm_net_mock_weapon_enhance_attrs) /
                         sizeof(g_vm_net_mock_weapon_enhance_attrs[0]));
        return g_vm_net_mock_weapon_enhance_attrs;
    case 1: /* 帽子 */
        *countOut = (u8)(sizeof(g_vm_net_mock_helm_enhance_attrs) /
                         sizeof(g_vm_net_mock_helm_enhance_attrs[0]));
        return g_vm_net_mock_helm_enhance_attrs;
    case 2: /* 上衣 */
        *countOut = (u8)(sizeof(g_vm_net_mock_chest_enhance_attrs) /
                         sizeof(g_vm_net_mock_chest_enhance_attrs[0]));
        return g_vm_net_mock_chest_enhance_attrs;
    case 3: /* 披风 */
        *countOut = (u8)(sizeof(g_vm_net_mock_cloak_enhance_attrs) /
                         sizeof(g_vm_net_mock_cloak_enhance_attrs[0]));
        return g_vm_net_mock_cloak_enhance_attrs;
    case 4: /* 束腰 */
        *countOut = (u8)(sizeof(g_vm_net_mock_belt_enhance_attrs) /
                         sizeof(g_vm_net_mock_belt_enhance_attrs[0]));
        return g_vm_net_mock_belt_enhance_attrs;
    case 5: /* 裤子 */
        *countOut = (u8)(sizeof(g_vm_net_mock_legs_enhance_attrs) /
                         sizeof(g_vm_net_mock_legs_enhance_attrs[0]));
        return g_vm_net_mock_legs_enhance_attrs;
    case 6: /* 鞋子 */
        *countOut = (u8)(sizeof(g_vm_net_mock_boots_enhance_attrs) /
                         sizeof(g_vm_net_mock_boots_enhance_attrs[0]));
        return g_vm_net_mock_boots_enhance_attrs;
    case 7: /* 饰品 */
        *countOut = (u8)(sizeof(g_vm_net_mock_ring_enhance_attrs) /
                         sizeof(g_vm_net_mock_ring_enhance_attrs[0]));
        return g_vm_net_mock_ring_enhance_attrs;
    default:
        *countOut = 0;
        return NULL;
    }
}

static bool vm_net_mock_seq_put_item_common_extra(u8 *out, u32 outCap,
                                                   u32 *pos,
                                                   u8 stackRuntimeByte,
                                                   u8 enhanceLevel,
                                                   u32 itemId)
{
    /*
     * JianghuOL.CBE:ParseEquipAttributes reads tagged i16, i16, u8 attr_count,
     * then attr_count slots of tagged (u8 unlock, u8 type, u8 flag, i16 value).
     * On-item attr arrays stop at index 6 (cmp #6) — never wire more than
     * VM_NET_MOCK_EQUIP_ATTR_CLIENT_CAP.
     *
     * Equipment wire pair (evidence: warehouse (1,0)→(+1); 29/1 writes
     * curlevel→item+0xe and maxlevel→item+0xf via 0x010287C0; backpack name
     * paint uses item+0xe for 「(+N)」):
     *   first i16  = current enhance level (low byte → +0xe)
     *   second i16 = maxlevel in low byte; high byte = attr_count when slots
     *                are present (强化附加 header denominator)
     * Non-equipment:
     *   first i16  = stackRuntimeByte
     *   second i16 = 0
     *
     * Slot layout when L>=1:
     *   1) L>=4: four milestone lines unlock 4/8/12/16 (slot template)
     *   2) then M(L)-base for 护甲/物攻/气血/法力 with base>0 (unlock=1),
     *      until CLIENT_CAP
     */
    const vm_net_mock_equipment_catalog_item *equip = NULL;
    const vm_net_mock_equip_enhance_attr_def *defs = NULL;
    u8 level = enhanceLevel;
    u8 attrCount = 0;
    u8 knownCount = 0;
    u16 firstWire = stackRuntimeByte;
    u16 secondWire = 0;
    u8 unlocks[VM_NET_MOCK_EQUIP_ATTR_CLIENT_CAP];
    u8 types[VM_NET_MOCK_EQUIP_ATTR_CLIENT_CAP];
    u8 flags[VM_NET_MOCK_EQUIP_ATTR_CLIENT_CAP];
    u16 values[VM_NET_MOCK_EQUIP_ATTR_CLIENT_CAP];
    u8 i = 0;

    if (level > VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL)
        level = (u8)VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL;
    if (itemId != 0)
        equip = vm_net_mock_find_equipment_catalog_item(itemId);
    if (equip != NULL)
    {
        firstWire = level;
        secondWire = (u16)VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL;
        if (level >= 4)
        {
            defs = vm_net_mock_equip_enhance_attrs_for_slot(equip->slot,
                                                            &knownCount);
            if (defs != NULL && knownCount > 0)
            {
                /*
                 * JianghuOL.CBE:0x01032118 paints every attr slot and chooses
                 * color by unlock vs current enhance (unlock > L → gray #5,
                 * else active #4).  Wire all 4 milestone lines whenever L>=4
                 * so +4 shows attr1 live and attrs 2..4 gray — not only the
                 * unlocked prefix (old attr_count=L/4 hid the rest).
                 */
                if (knownCount > VM_NET_MOCK_EQUIP_ATTR_MILESTONE_SLOTS)
                    knownCount = VM_NET_MOCK_EQUIP_ATTR_MILESTONE_SLOTS;
                for (i = 0; i < knownCount; ++i)
                {
                    u8 realUnlock = (u8)((i + 1u) * 4u);
                    unlocks[attrCount] = vm_net_mock_equip_enhance_wire_unlock(
                        defs[i].type, realUnlock);
                    types[attrCount] =
                        vm_net_mock_equip_enhance_wire_type(defs[i].type);
                    flags[attrCount] = defs[i].flag;
                    values[attrCount] =
                        vm_net_mock_equip_enhance_attr_value_for_type(
                            equip->quality, defs[i].type, defs[i].flag);
                    attrCount++;
                }
            }
        }
        /*
         * M(L) 护甲/物攻：强化缩放相对基值的增量，接在里程碑后面。
         * unlock=1 → 详情 "(+1)护甲+Δ"（勿用 255）。F8/武器路径仍负责实际加成。
         */
        if (level >= 1)
        {
            static const u8 kScaleExtraTypes[] = {
                VM_NET_MOCK_EQUIP_ATTR_ARMOR,
                VM_NET_MOCK_EQUIP_ATTR_ATTACK
            };

            for (i = 0; i < (u8)(sizeof(kScaleExtraTypes) /
                                 sizeof(kScaleExtraTypes[0]));
                 ++i)
            {
                u8 type = kScaleExtraTypes[i];
                u32 base = 0;
                u32 delta;

                if (attrCount >= VM_NET_MOCK_EQUIP_ATTR_CLIENT_CAP)
                    break;
                switch (type)
                {
                case VM_NET_MOCK_EQUIP_ATTR_ARMOR:
                    base = equip->bonus.armor;
                    break;
                case VM_NET_MOCK_EQUIP_ATTR_ATTACK:
                    base = equip->bonus.attack;
                    break;
                default:
                    break;
                }
                if (base == 0)
                    continue;
                delta = vm_net_mock_equipment_bonus_enhance_delta(base, level);
                if (delta == 0)
                    continue;
                if (delta > 0x7FFFu)
                    delta = 0x7FFFu;
                unlocks[attrCount] = 1;
                types[attrCount] =
                    vm_net_mock_equip_enhance_wire_type(type);
                flags[attrCount] = 0;
                values[attrCount] = (u16)delta;
                attrCount++;
            }
        }

        if (attrCount > 0)
        {
            secondWire = (u16)(VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL |
                               ((u16)attrCount << 8));
        }
    }

    if (!vm_net_mock_seq_put_i16(out, outCap, pos, firstWire))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, pos, secondWire))
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, pos, attrCount))
        return false;

    for (i = 0; i < attrCount; ++i)
    {
        if (!vm_net_mock_seq_put_u8(out, outCap, pos, unlocks[i]) ||
            !vm_net_mock_seq_put_u8(out, outCap, pos, types[i]) ||
            !vm_net_mock_seq_put_u8(out, outCap, pos, flags[i]) ||
            !vm_net_mock_seq_put_i16(out, outCap, pos, values[i]))
        {
            return false;
        }
    }
    return true;
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
    u8 rowCount = vm_net_mock_role_backpack_client_grid_count(role);
    if (out == NULL || blobLenOut == NULL)
        return false;
    if (rowCountOut)
        *rowCountOut = 0;

    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, rowCount))
        return false;
    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        if (!vm_net_mock_backpack_item_is_client_grid_item(item))
            continue;
        if (!vm_net_mock_seq_put_u32(out, outCap, &pos, item->itemId))
            return false;
        if (!vm_net_mock_seq_put_item_common_extra(
                out, outCap, &pos, vm_net_mock_backpack_stack_byte(item),
                (u8)SDL_min(item->enhanceLevel,
                            VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL),
                item->itemId))
        {
            return false;
        }
    }
    *blobLenOut = pos;
    if (rowCountOut)
        *rowCountOut = rowCount;
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
                                                       item->stack, 0,
                                                       item->itemId))
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
    u8 gridCount = vm_net_mock_role_backpack_client_grid_count(role);
    if (out == NULL || blobLenOut == NULL)
        return false;
    if (gridCountOut)
        *gridCountOut = 0;

    for (u32 i = 0; i < itemCount; ++i)
    {
        const vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        if (!vm_net_mock_backpack_item_is_client_grid_item(item))
            continue;
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
                            VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL),
                item->itemId))
            return false;
    }
    *blobLenOut = pos;
    if (gridCountOut)
        *gridCountOut = gridCount;
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
        /*
         * mmShopMstarWqvga.cbm:sub_7BC stores this byte at row+14 and the list
         * painter treats it as a small quality/status index (same palette as
         * equip 品质: 0 black/white, 1 green, …). item.dsh "形象" for many
         * category-10 rows is an icon id such as 13, which falls outside that
         * palette: names render black and the buy UI rejects the row. Secret
         * mall rows (subtype 5) therefore clamp to the green index used by
         * typical 类别=14 rows (形象=1). Equipment pages keep DSH visual.
         */
        {
            u8 wireVisual = item->visual;

            if (subtype == 5 && (wireVisual == 0 || wireVisual > 4))
                wireVisual = 1;
            if (!vm_net_mock_seq_put_u32(out, outCap, &pos, item->itemId))
                return false;
            if (!vm_net_mock_seq_put_string(out, outCap, &pos, item->name))
                return false;
            if (!vm_net_mock_seq_put_u8(out, outCap, &pos, wireVisual))
                return false;
        }
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

static bool vm_net_mock_build_item_use_count_info_blob(u8 *out, u32 outCap,
                                                       u16 seq, u32 count,
                                                       u32 *blobLenOut);

/* In-scene actorinfo refresh after map/battle revival.
 * Prefer 1/1/14 (same object mmShop:0x9DE / parse_actorinfo_response consumes
 * for live actor-state updates).  Login-shaped 1/1/1 is easy to ignore on the
 * scene-sync poll path while mmGame already owns the map HUD. */
static u32 vm_net_mock_build_map_actor_vitals_sync_response_ex(
    u8 *out,
    u32 outCap,
    u16 bagClearSeq,
    u32 bagClearRemaining)
{
    u32 pos = 5;
    u8 objectCount = 0;
    u32 actorInfoLen = 0;
    u8 countInfo[16];
    u32 countInfoLen = 0;
    u32 objectStart = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_append_shop_actor_state14_object(out, outCap, &pos,
                                                      &actorInfoLen))
    {
        return 0;
    }
    objectCount += 1;

    if (bagClearSeq != 0)
    {
        if (!vm_net_mock_build_item_use_count_info_blob(
                countInfo, sizeof(countInfo), bagClearSeq, bagClearRemaining,
                &countInfoLen) ||
            !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 11,
                                         &objectStart) ||
            !vm_net_mock_put_object_raw(out, outCap, &pos, "info", countInfo,
                                        (u16)countInfoLen))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    return pos;
}

static u32 vm_net_mock_build_map_actor_vitals_sync_response(u8 *out, u32 outCap)
{
    return vm_net_mock_build_map_actor_vitals_sync_response_ex(out, outCap, 0, 0);
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
    /* Extra 1/7/4 bodies in the same WT (objectCount often still 1). */
    u8 trailingDiscardObjects;
    bool haveActorOtherCompanion;
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

static bool vm_net_mock_get_object_tagged_number_entry(
    const u8 *payload, u32 payloadLen, const char *field, u32 *valueOut);

/* Prefer the length-prefixed object-entry encoding used by live CBE writers
 * (value = `00 <width> <be>`).  The legacy u32 scanner below treats the entry's
 * outer `00 04` length as a raw i32 tag, so a u16 such as itemseq=26 becomes
 * 0x0002001A and fails every `seq <= 0xffff` guard — including exp-card 7/30. */
static bool vm_net_mock_get_object_number_field(const u8 *payload, u32 payloadLen,
                                                const char *field, u32 *value)
{
    u32 value32 = 0;
    u16 value16 = 0;
    u8 value8 = 0;

    if (value)
        *value = 0;
    if (vm_net_mock_get_object_tagged_number_entry(payload, payloadLen, field,
                                                   &value32))
    {
        if (value)
            *value = value32;
        return true;
    }
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

/* Length-prefixed object entries encode numbers as `00 <width> <be>`.
 * SendEquipSequenceReq (0x0101DD1E) and exp-card Send at 0x010236CA both use
 * this shape for u16 sequence fields. */
static bool vm_net_mock_get_object_tagged_number_entry(
    const u8 *payload, u32 payloadLen, const char *field, u32 *valueOut)
{
    const u8 *entry = NULL;
    u16 entryLen = 0;
    u32 value = 0;

    if (valueOut)
        *valueOut = 0;
    if (!vm_net_mock_get_object_entry_bytes(payload, payloadLen, field, &entry,
                                            &entryLen) ||
        entry == NULL || entryLen < 3 || entry[0] != 0)
    {
        return false;
    }
    switch (entry[1])
    {
    case 1:
        if (entryLen != 3)
            return false;
        value = entry[2];
        break;
    case 2:
        if (entryLen != 4)
            return false;
        value = ((u32)entry[2] << 8) | entry[3];
        break;
    case 4:
        if (entryLen != 6)
            return false;
        value = ((u32)entry[2] << 24) | ((u32)entry[3] << 16) |
                ((u32)entry[4] << 8) | entry[5];
        break;
    default:
        return false;
    }
    if (valueOut)
        *valueOut = value;
    return true;
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
    char trailingDump[96];
    u32 trailingDumpPos = 0;

    if (parsedOut)
        memset(parsedOut, 0, sizeof(*parsedOut));
    memset(&parsed, 0, sizeof(parsed));
    trailingDump[0] = 0;

    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object))
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

    /*
     * Continuous discard can flush a second 1/7/4 into the same WT while
     * objectCount stays 1 (runtime: len=72 first=1/7/4:29,1/7/4:29).  Requiring
     * offset==requestLen after one object rejected the packet → response=0 →
     * JianghuOL.CBE:0x01033544 never clears the item-op wait flag.
     *
     * After a valid primary selector, never fail the whole request on trailing
     * bytes: accept extra 7/4 / optional 2/10, and skip any other trailing
     * object or undecodable tail so the wait flag still clears.
     */
    while (offset < requestLen)
    {
        vm_net_mock_request_object trailing;
        u32 trailSeq = 0;
        u32 trailItem = 0;
        u32 beforeOffset = offset;
        int wrote;

        if (!vm_net_mock_next_request_object(request, requestLen, &offset, &trailing))
        {
            printf("[warn][network] mock_item_discard_trailing_skip "
                   "len=%u offset=%u leftover=%u "
                   "evidence=undecodable-tail-keep-primary-7/4\n",
                   requestLen, beforeOffset, requestLen - beforeOffset);
            break;
        }
        if (trailing.major == 1 && trailing.kind == 7 && trailing.subtype == 4 &&
            trailing.payloadLen != 0)
        {
            if (parsed.trailingDiscardObjects < 0xff)
                parsed.trailingDiscardObjects += 1;
            (void)vm_net_mock_get_object_number_field(trailing.payload,
                                                      trailing.payloadLen,
                                                      "seq", &trailSeq);
            if (trailSeq == 0)
            {
                (void)vm_net_mock_get_object_number_field(trailing.payload,
                                                          trailing.payloadLen,
                                                          "itemseq", &trailSeq);
            }
            (void)vm_net_mock_get_object_number_field(trailing.payload,
                                                      trailing.payloadLen,
                                                      "itemId", &trailItem);
            if (trailItem == 0)
            {
                (void)vm_net_mock_get_object_number_field(trailing.payload,
                                                          trailing.payloadLen,
                                                          "id", &trailItem);
            }
            wrote = snprintf(trailingDump + trailingDumpPos,
                             sizeof(trailingDump) - trailingDumpPos,
                             "%s7/4:seq=%u/item=%u",
                             trailingDumpPos ? "," : "",
                             trailSeq, trailItem);
            if (wrote > 0 &&
                (u32)wrote < sizeof(trailingDump) - trailingDumpPos)
            {
                trailingDumpPos += (u32)wrote;
            }
            continue;
        }
        if (trailing.major == 1 && trailing.kind == 2 && trailing.subtype == 10 &&
            trailing.payloadLen == 10)
        {
            parsed.haveActorOtherCompanion = true;
            wrote = snprintf(trailingDump + trailingDumpPos,
                             sizeof(trailingDump) - trailingDumpPos,
                             "%s2/10",
                             trailingDumpPos ? "," : "");
            if (wrote > 0 &&
                (u32)wrote < sizeof(trailingDump) - trailingDumpPos)
            {
                trailingDumpPos += (u32)wrote;
            }
            continue;
        }
        wrote = snprintf(trailingDump + trailingDumpPos,
                         sizeof(trailingDump) - trailingDumpPos,
                         "%s%u/%u/%u:%u",
                         trailingDumpPos ? "," : "",
                         trailing.major, trailing.kind, trailing.subtype,
                         trailing.payloadLen);
        if (wrote > 0 &&
            (u32)wrote < sizeof(trailingDump) - trailingDumpPos)
        {
            trailingDumpPos += (u32)wrote;
        }
        printf("[warn][network] mock_item_discard_trailing_skip "
               "len=%u primary_seq=%u skip=%u/%u/%u:%u "
               "evidence=unknown-trailing-keep-primary-7/4\n",
               requestLen, parsed.seq, trailing.major, trailing.kind,
               trailing.subtype, trailing.payloadLen);
    }

    if (parsed.trailingDiscardObjects != 0 || parsed.haveActorOtherCompanion ||
        trailingDump[0] != 0)
    {
        printf("[warn][network] mock_item_discard_compound len=%u primary_seq=%u "
               "primary_item=%u trailing_7_4=%u actor_other=%u trail=[%s] "
               "evidence=same-WT-multi-7/4-must-still-clear-wait\n",
               requestLen, parsed.seq, parsed.itemId,
               parsed.trailingDiscardObjects,
               parsed.haveActorOtherCompanion ? 1u : 0u,
               trailingDump[0] ? trailingDump : "-");
    }

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
                                                     u32 count, u16 enhanceLevel,
                                                     u32 *blobLenOut)
{
    u32 pos = 0;
    u8 stackRuntime = 0;
    u8 enhance =
        (u8)SDL_min(enhanceLevel, VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL);

    if (blobLenOut)
        *blobLenOut = 0;
    if (out == NULL || blobLenOut == NULL || itemId == 0)
        return false;
    /*
     * Backpack 7/7 / grant paths: count is instance/stack quantity.  Equipment
     * must stay 1 here — MoveBattleActorStep / bag insert loop on count, and
     * 7/11 may rewrite item+0xf2.  Worn-slot durability is only the login
     * equipment blob (mmGame:0xD04 → item+0x110).
     */
    stackRuntime = vm_net_mock_item_common_extra_stack_byte(itemId, count);
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seq))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, itemId))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, count))
        return false;
    if (!vm_net_mock_seq_put_item_common_extra(out, outCap, &pos, stackRuntime,
                                                enhance, itemId))
        return false;

    *blobLenOut = pos;
    return true;
}

/*
 * JianghuOL.CBE runtime 0x0102617a.. (IDA file-abs 0x01026214, 7/15 result=1):
 *   total drives the reward loop; the reward blob field name is iteminfo
 *   (PC-rel at 0x01026188 resolves to "iteminfo", not the nearby "info"
 *   pool bytes).  Blob has NO leading row-count.  Each row is:
 *     reader+0x20 tagged-u32 itemId
 *     reader+0x24 tagged-i16 seq
 *     reader+0x20 tagged-u32 count
 *   then the item-manager helper consumes ParseEquipAttributes-shaped
 *   common-extra from the same stream.  Reusing 7/7 iteminfo (rowCount +
 *   seq-first) misaligns the first i32 read; naming the field "info"
 *   makes getter +0x28 return NULL and faults at stream_read (~0x01033A68)
 *   on [NULL+4].
 */
static bool vm_net_mock_build_chest_open_info_blob(u8 *out, u32 outCap,
                                                   u16 seq, u32 itemId,
                                                   u32 count, u32 *blobLenOut)
{
    u32 pos = 0;

    if (blobLenOut)
        *blobLenOut = 0;
    if (out == NULL || blobLenOut == NULL || itemId == 0 || seq == 0)
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, itemId))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seq))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, count))
        return false;
    if (!vm_net_mock_seq_put_item_common_extra(
            out, outCap, &pos,
            vm_net_mock_item_common_extra_stack_byte(itemId, count), 0,
            itemId))
        return false;

    *blobLenOut = pos;
    return true;
}

typedef struct
{
    u32 itemId;
    u16 seq;
    u32 count;
    u16 enhanceLevel;
} vm_net_mock_battle_drop_result;

#define VM_NET_MOCK_BATTLE_DROP_RESULT_MAX 8u

/* The client-side 7/7 type=1 stream starts with a row count and can safely
 * consume several normal additive item rows.  Keep this builder shared so
 * battle rewards and ordinary single-item operations use the exact same
 * common-extra representation. */
static bool vm_net_mock_build_item_use_iteminfo_rows_blob(
    u8 *out, u32 outCap, const vm_net_mock_battle_drop_result *rows,
    u8 rowCount, u32 *blobLenOut)
{
    u32 pos = 0;

    if (blobLenOut)
        *blobLenOut = 0;
    if (out == NULL || rows == NULL || blobLenOut == NULL || rowCount == 0 ||
        rowCount > VM_NET_MOCK_BATTLE_DROP_RESULT_MAX)
    {
        return false;
    }
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, rowCount))
        return false;
    for (u8 i = 0; i < rowCount; ++i)
    {
        u8 stackRuntime = 0;
        u8 enhance =
            (u8)SDL_min(rows[i].enhanceLevel,
                        VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL);

        stackRuntime = vm_net_mock_item_common_extra_stack_byte(rows[i].itemId,
                                                               rows[i].count);
        if (rows[i].seq == 0 || rows[i].itemId == 0 || rows[i].count == 0 ||
            !vm_net_mock_seq_put_i16(out, outCap, &pos, rows[i].seq) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, rows[i].itemId) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, rows[i].count) ||
            !vm_net_mock_seq_put_item_common_extra(out, outCap, &pos,
                                                    stackRuntime, enhance,
                                                    rows[i].itemId))
        {
            return false;
        }
    }
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
    u8 *out, u32 outCap, u32 *pos, u16 seq, u32 itemId, u32 count,
    u16 enhanceLevel)
{
    u8 itemInfo[VM_NET_MOCK_ITEM_USE_ITEMINFO_SCRATCH];
    u32 itemInfoLen = 0;
    u32 objectStart = 0;

    if (out == NULL || pos == NULL || seq == 0 || itemId == 0 || count == 0)
        return false;
    if (!vm_net_mock_build_item_use_iteminfo_blob(
            itemInfo, sizeof(itemInfo), seq, itemId, count, enhanceLevel,
            &itemInfoLen) ||
        itemInfoLen == 0 || itemInfoLen > 0xffffu)
    {
        printf("[error][network] mock_backpack_add_encode_failed item=%u seq=%u "
               "enhance=%u scratch=%u evidence=iteminfo-overflow-7/7-type1 "
               "warehouse-retrieve-or-npc-buy\n",
               itemId, seq, enhanceLevel, (u32)sizeof(itemInfo));
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

    printf("[info][network] mock_backpack_add item=%u seq=%u delta=%u enhance=%u iteminfo_len=%u response=7/7-type1 evidence=mmGame:0x11CE+0x0D04\n",
           itemId, seq, count, enhanceLevel, itemInfoLen);
    vm_autotest_note("mock_backpack_add item=%u seq=%u delta=%u enhance=%u iteminfo_len=%u response=7/7-type1 evidence=mmGame:0x11CE+0x0D04\n",
                     itemId, seq, count, enhanceLevel, itemInfoLen);
    return true;
}

/*
 * Rewrite one backpack seq via 7/7 so a following 7/11=0 can flask-delete it.
 * peelType 1 → +52 (bag ProcessItem); peelType 2 → +104 (item-use alternate).
 */
static bool vm_net_mock_append_backpack_item_morph7_object(
    u8 *out, u32 outCap, u32 *pos, u16 seq, u32 morphItemId, u32 morphCount,
    u8 peelType, u32 originalItemId)
{
    u8 itemInfo[VM_NET_MOCK_ITEM_USE_ITEMINFO_SCRATCH];
    u32 itemInfoLen = 0;
    u32 objectStart = 0;

    if (out == NULL || pos == NULL || seq == 0 || morphItemId == 0 ||
        morphCount == 0 || (peelType != 1 && peelType != 2))
    {
        return false;
    }
    if (!vm_net_mock_build_item_use_iteminfo_blob(
            itemInfo, sizeof(itemInfo), seq, morphItemId, morphCount, 0,
            &itemInfoLen) ||
        itemInfoLen == 0 || itemInfoLen > 0xffffu)
    {
        return false;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 7, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "type", peelType) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", itemInfo,
                                    (u16)itemInfoLen))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    printf("[info][network] mock_backpack_remove item=%u seq=%u morph=%u "
           "count=%u type=%u iteminfo_len=%u response=7/7-type%u "
           "evidence=mmGame:0x0D04+JianghuOL:0x01033544-flask-delete-via-morph\n",
           originalItemId, seq, morphItemId, morphCount, peelType, itemInfoLen,
           peelType);
    return true;
}

/*
 * Warehouse deposit bag peel for ordinary consumables (later poll, never
 * inside 26/1): rewrite seq→802 via 7/7 type=2, then caller sends 7/11=0 so
 * HandleItemOperationResponse takes the flask row-delete branch.
 *
 * Equipment must NOT use this helper — type=2 goes to item-manager +104 with
 * r2=-1 and does not rewrite bag gear itemId.  Equipment deposit peel uses
 * the discard-shaped 7/4+17/1+7/42+7/11 path instead (no copper refund).
 */
static bool vm_net_mock_append_backpack_item_remove7_object(
    u8 *out, u32 outCap, u32 *pos, u16 seq, u32 itemId)
{
    if (out == NULL || pos == NULL || seq == 0 || itemId == 0)
        return false;
    if (vm_net_mock_backpack_item_id_uses_reservoir_count(itemId))
        return true;
    if (vm_net_mock_find_equipment_catalog_item(itemId) != NULL)
        return false;
    return vm_net_mock_append_backpack_item_morph7_object(
        out, outCap, pos, seq, 802u, 1u, 2u, itemId);
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

/*
 * JianghuOL.CBE:0x01033544 reads 7/11.info as row_count + i16 seq + u32 value.
 * For 802/803 that value is the HP/MP reservoir; visible stack stays 1.
 */
static bool vm_net_mock_append_backpack_reservoir_count7_11_object(
    u8 *out, u32 outCap, u32 *pos, u16 seq, u32 reservoir)
{
    u8 info[16];
    u32 infoLen = 0;
    u32 objectStart = 0;

    if (out == NULL || pos == NULL || seq == 0)
        return false;
    if (!vm_net_mock_build_item_use_count_info_blob(
            info, sizeof(info), seq, reservoir, &infoLen) ||
        infoLen == 0 || infoLen > 0xffffu)
    {
        return false;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 11, &objectStart) ||
        !vm_net_mock_put_object_raw(out, outCap, pos, "info", info, (u16)infoLen))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    printf("[info][network] mock_backpack_reservoir_row seq=%u value=%u response=7/11 evidence=JianghuOL.CBE:0x01033544\n",
           seq, reservoir);
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
        u32 durability = vm_net_mock_equipment_durability_max_for_item(itemId);

        /* mmGameMstarWqvga.cbm:sub_D04 reads every 7/7 row as
         * seq(u16), itemId(u32), current-count(u32), and the common equipment
         * attributes.  For item ids >= 1000 it writes current-count to the
         * equipment current-durability field at item+272. */
        if (itemId == 0 || durability == 0 ||
            vm_net_mock_find_equipment_catalog_item(itemId) == NULL)
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
            !vm_net_mock_seq_put_item_common_extra(
                out, outCap, &pos, 0,
                (u8)SDL_min(role->equippedEnhanceLevels[slot],
                            VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL),
                itemId))
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

static bool vm_net_mock_shop_item_is_direct_gold_yuanbao(u32 itemId)
{
    /*
     * mmShopMstarWqvga.cbm:sub_9DE (0xCEE): after a successful type=2 buy it
     * builds 808 as (0x65<<3) and jumps straight to the no-insert path. Unlike
     * 806 it does not mutate any local currency field there, so the server must
     * grant copper and push actorinfo for the HUD to move.
     */
    return itemId == VM_NET_MOCK_GOLD_YUANBAO_ITEM_ID;
}

static u32 vm_net_mock_shop_gold_yuanbao_copper_grant(u8 count)
{
    u32 goldUnits = vm_net_mock_mul_capped_u32(
        VM_NET_MOCK_GOLD_YUANBAO_GOLD_UNITS, count ? count : 1u);
    return vm_net_mock_mul_capped_u32(goldUnits,
                                      VM_NET_MOCK_MONEY_COPPER_PER_GOLD);
}

static u8 vm_net_mock_shop_buy14_insufficient_funds_result(u8 type)
{
    /*
     * mmShopMstarWqvga.cbm:sub_9DE (0xD3E) / UI (0x38C4):
     *   result==2 && type==2 -> shop state +0xf=2 ->
     *     "酷宝和礼券不足，是否进入充值？"
     * Returning 0 leaves the local loading flag set (looks like a hang).
     * Do NOT reuse result=2 for backpack-full: that is result=3
     * ("背包已满，请扩容或整理背包后再购买。").
     */
    return type == 2 ? 2 : 0;
}

static u8 vm_net_mock_shop_buy14_backpack_fail_result(u8 type)
{
    /*
     * result==3 -> state +0xf=3, flag=0 -> bag-full toast; for item 806 the
     * same state shows the expand-cap message instead.
     * result==4 -> state +0xf=3, flag=1 -> gift-pack free-slot message.
     * Non-W-coin buys still cannot use 0 (loading hang); mirror result=3.
     */
    (void)type;
    return 3;
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
    if (current >= VM_NET_MOCK_BACKPACK_CLIENT_LOGICAL_CAPACITY)
        return 0;
    room = VM_NET_MOCK_BACKPACK_CLIENT_LOGICAL_CAPACITY - current;
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
    if (newCapacity > VM_NET_MOCK_BACKPACK_CLIENT_LOGICAL_CAPACITY)
        newCapacity = VM_NET_MOCK_BACKPACK_CLIENT_LOGICAL_CAPACITY;
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

static bool vm_net_mock_parse_special_item_seq_request(
    const u8 *request, u32 requestLen, u8 kind, u8 subtype,
    const char *seqField, bool requireOneNum, u16 *seqOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;
    u32 sequence = 0;
    u32 num = 0;

    if (seqOut)
        *seqOut = 0;
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T' ||
        seqField == NULL || !vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != kind ||
        object.subtype != subtype ||
        !vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              seqField, &sequence) ||
        sequence == 0 || sequence > 0xffffu)
    {
        return false;
    }
    if (requireOneNum &&
        (!vm_net_mock_get_object_number_field(object.payload, object.payloadLen, "num", &num) ||
         num != 1))
    {
        return false;
    }
    if (seqOut)
        *seqOut = (u16)sequence;
    return true;
}

static u32 vm_net_mock_exp_card_multiplier_for_item(u32 itemId)
{
    switch (itemId)
    {
    case 809:
        return 2;
    case 810:
        return 4;
    case 811:
        return 10;
    case 845:
        return 30;
    default:
        return 0;
    }
}

static const char *vm_net_mock_special_item_success_info(u32 itemId)
{
    /* GBK literals copied from item.dsh descriptions.  The CBE client renders
     * packet strings as GBK, so UTF-8 source literals would be visually wrong. */
    switch (itemId)
    {
    case 809:
        return "\xBB\xF1\xB5\xC3\x32\xB1\xB6\xBE\xAD\xD1\xE9\xD6\xB5\xA3\xAC\xB3\xD6\xD0\xF8\xCA\xB1\xBC\xE4\x31\xD0\xA1\xCA\xB1\xA1\xA3";
    case 810:
        return "\xBB\xF1\xB5\xC3\x34\xB1\xB6\xBE\xAD\xD1\xE9\xD6\xB5\xA3\xAC\xB3\xD6\xD0\xF8\xCA\xB1\xBC\xE4\x31\xD0\xA1\xCA\xB1\xA1\xA3";
    case 811:
        return "\xBB\xF1\xB5\xC3\x31\x30\xB1\xB6\xBE\xAD\xD1\xE9\xD6\xB5\xA3\xAC\xB3\xD6\xD0\xF8\xCA\xB1\xBC\xE4\x31\xD0\xA1\xCA\xB1\xA1\xA3";
    case 845:
        return "\xBB\xF1\xB5\xC3\x33\x30\xB1\xB6\xBE\xAD\xD1\xE9\xD6\xB5\xA3\xAC\xB3\xD6\xD0\xF8\xCA\xB1\xBC\xE4\x31\xD0\xA1\xCA\xB1\xA1\xA3";
    case 829:
        return "\xCA\xA7\xB4\xAB\xD2\xD1\xBE\xC3\xB5\xC4\xC9\xF1\xC3\xD8\xB5\xA4\xD2\xA9\xA3\xAC\xB7\xFE\xD3\xC3\xBA\xF3\x33\x30\xB7\xD6\xD6\xD3\xC4\xDA\xC9\xCB\xBA\xA6\xBA\xCD\xB7\xC0\xD3\xF9\xD0\xA7\xB9\xFB\xC3\xF7\xCF\xD4\xCC\xE1\xC9\xFD\xA1\xA3";
    case 830:
        return "\xCA\xA7\xB4\xAB\xD2\xD1\xBE\xC3\xB5\xC4\xC9\xF1\xC3\xD8\xB5\xA4\xD2\xA9\xA3\xAC\xB7\xFE\xD3\xC3\xBA\xF3\x33\x30\xB7\xD6\xD6\xD3\xC4\xDA\xC9\xCB\xBA\xA6\xBA\xCD\xB7\xC0\xD3\xF9\xD0\xA7\xB9\xFB\xBE\xDE\xB7\xF9\xCC\xE1\xC9\xFD\xA3\xAC\xBC\xF2\xD6\xB1\xCA\xC7\xC8\xCB\xB5\xB2\xC9\xB1\xC8\xC8\xCB\xB7\xF0\xB5\xB2\xC9\xB1\xB7\xF0\xB0\xA1\xA3";
    default:
        return "OK";
    }
}

/* JianghuOL.CBE:0x01011AF8 paints 7/31 `expinfo` as the status tip text.
 * Append remaining wall-clock time from account_role_item_effects so stacked
 * cards show how long the buff still lasts.  GBK literals match the client. */
static const char *vm_net_mock_exp_card_active_info(u32 multiplier,
                                                   u32 remainingSeconds)
{
    static char info[96];
    const char *prefix;
    u32 remainMin;

    switch (multiplier)
    {
    case 2:
        prefix = "\xCB\xAB\xB1\xB6\xBE\xAD\xD1\xE9\xBF\xA8\xC9\xFA\xD0\xA7\xD6\xD0";
        break;
    case 4:
        prefix = "\xCB\xC4\xB1\xB6\xBE\xAD\xD1\xE9\xBF\xA8\xC9\xFA\xD0\xA7\xD6\xD0";
        break;
    case 10:
        prefix = "\xCA\xAE\xB1\xB6\xBE\xAD\xD1\xE9\xBF\xA8\xC9\xFA\xD0\xA7\xD6\xD0";
        break;
    case 30:
        prefix = "\xC8\xFD\xCA\xAE\xB1\xB6\xBE\xAD\xD1\xE9\xBF\xA8\xC9\xFA\xD0\xA7\xD6\xD0";
        break;
    default:
        return "";
    }

    if (remainingSeconds == 0)
        return prefix;

    /* Ceil to whole minutes so sub-minute leftovers still read as 1 minute. */
    remainMin = (remainingSeconds + 59u) / 60u;
    if (remainMin == 0)
        remainMin = 1;
    if (remainMin >= 60u)
    {
        snprintf(info, sizeof(info),
                 "%s(\xCA\xA3\xD3\xE0%u\xD0\xA1\xCA\xB1%u\xB7\xD6)",
                 prefix, remainMin / 60u, remainMin % 60u);
    }
    else
    {
        snprintf(info, sizeof(info),
                 "%s(\xCA\xA3\xD3\xE0%u\xB7\xD6\xD6\xD3)",
                 prefix, remainMin);
    }
    return info;
}

/* Append 「(剩余XhYm)」 / 「(剩余N分钟)」; returns false when remaining is 0. */
static bool vm_net_mock_append_remaining_time_suffix(char *out, u32 outCap,
                                                    u32 used, u32 remainingSeconds)
{
    u32 remainMin;

    if (out == NULL || outCap <= used || remainingSeconds == 0)
        return false;
    remainMin = (remainingSeconds + 59u) / 60u;
    if (remainMin == 0)
        remainMin = 1;
    if (remainMin >= 60u)
    {
        snprintf(out + used, outCap - used,
                 "(\xCA\xA3\xD3\xE0%u\xD0\xA1\xCA\xB1%u\xB7\xD6)",
                 remainMin / 60u, remainMin % 60u);
    }
    else
    {
        snprintf(out + used, outCap - used,
                 "(\xCA\xA3\xD3\xE0%u\xB7\xD6\xD6\xD3)",
                 remainMin);
    }
    return true;
}

/* Same tip format as exp cards; prefix is 「战斗心得生效中」. */
static const char *vm_net_mock_battle_insight_active_info(u32 remainingSeconds)
{
    static char info[96];
    const char *prefix =
        "\xD5\xBD\xB6\xB7\xD0\xC4\xB5\xC3\xC9\xFA\xD0\xA7\xD6\xD0";

    if (remainingSeconds == 0)
        return prefix;
    snprintf(info, sizeof(info), "%s", prefix);
    (void)vm_net_mock_append_remaining_time_suffix(info, sizeof(info),
                                                   (u32)strlen(info),
                                                   remainingSeconds);
    return info;
}

/* 25/6 iteminfo toast: 「战斗心得效果已生效，经验增加20%(剩余…)。」 */
static const char *vm_net_mock_battle_insight_use_success_info(u32 remainingSeconds)
{
    static char info[128];
    const char *base =
        "\xD5\xBD\xB6\xB7\xD0\xC4\xB5\xC3\xD0\xA7\xB9\xFB\xD2\xD1\xC9\xFA\xD0\xA7"
        "\xA3\xAC\xBE\xAD\xD1\xE9\xD4\xF6\xBC\xD3\x32\x30\x25";
    const char *period = "\xA1\xA3";

    snprintf(info, sizeof(info), "%s", base);
    (void)vm_net_mock_append_remaining_time_suffix(info, sizeof(info),
                                                   (u32)strlen(info),
                                                   remainingSeconds);
    {
        size_t used = strlen(info);
        if (used + 2 < sizeof(info))
        {
            info[used] = period[0];
            info[used + 1] = period[1];
            info[used + 2] = '\0';
        }
    }
    return info;
}

/*
 * JianghuOL.CBE:0x01011A5E (HandleExpInfoResponse) shows the top-left status
 * icon from a non-empty 7/31 `expinfo`.  The bare `expcard` flag from 7/32 or
 * actor login is not enough for that UI — battle reward can still apply the
 * MySQL multiplier while the icon stays hidden.  Push 7/31 whenever an
 * experience card or battle insight is active.  One client slot: prefer the
 * card tip when both are active.
 */
static bool vm_net_mock_append_exp_card_info_object(u8 *out, u32 outCap, u32 *pos)
{
    vm_net_mock_role_state *role;
    u32 multiplier;
    u32 remainingSeconds;
    u32 objectStart = 0;
    const char *info;

    if (out == NULL || pos == NULL)
        return false;
    role = vm_net_mock_active_role();
    multiplier = vm_net_mock_role_active_exp_card_multiplier(role);
    if (multiplier > 1u)
    {
        remainingSeconds = vm_net_mock_role_active_exp_card_remaining_seconds(role);
        info = vm_net_mock_exp_card_active_info(multiplier, remainingSeconds);
    }
    else if (vm_net_mock_role_active_battle_exp_bonus_percent(role) != 0)
    {
        remainingSeconds =
            vm_net_mock_role_active_battle_insight_remaining_seconds(role);
        info = vm_net_mock_battle_insight_active_info(remainingSeconds);
    }
    else
    {
        return true;
    }
    if (info == NULL || info[0] == '\0')
        return false;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 31, &objectStart) ||
        !vm_net_mock_put_object_string(out, outCap, pos, "expinfo", info))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return true;
}

/* After a path that does not locally write expcard (25/6 insight, custom 7/1
 * cards), arm the shared icon gate: 7/32{expcard=1} + 7/31{expinfo}. */
static bool vm_net_mock_append_status_icon_arm_objects(u8 *out, u32 outCap,
                                                       u32 *pos,
                                                       u8 *extraObjectsOut)
{
    u32 objectStart = 0;

    if (extraObjectsOut)
        *extraObjectsOut = 0;
    if (out == NULL || pos == NULL)
        return false;
    if (vm_net_mock_role_active_status_icon_flag() == 0)
        return true;
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 32, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "expcard", 1))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    if (!vm_net_mock_append_exp_card_info_object(out, outCap, pos))
        return false;
    if (extraObjectsOut)
        *extraObjectsOut = 2;
    return true;
}

/* Login group / scene sync replacement for the old always-7/32 slot:
 * active card/insight -> 7/31 {expinfo}; inactive -> 7/32 {result,expcard=0}. */
static u8 vm_net_mock_append_exp_card_ui_objects(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;

    if (out == NULL || pos == NULL)
        return 0;
    if (vm_net_mock_role_active_status_icon_flag() != 0)
    {
        if (!vm_net_mock_append_exp_card_info_object(out, outCap, pos))
            return 0;
        return 1;
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 7, 32, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "result", 1) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "expcard", 0))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    return 1;
}

static bool vm_net_mock_is_exp_card_status_request(const u8 *request,
                                                   u32 requestLen)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    return request != NULL && requestLen >= 9 && request[0] == 'W' &&
           request[1] == 'T' &&
           vm_net_mock_next_request_object(request, requestLen, &offset, &object) &&
           offset == requestLen && object.major == 1 && object.kind == 7 &&
           object.subtype == 31;
}

/* SendSceneAction31 is the authoritative refresh point for an active
 * experience card / battle-insight tip.  The response shape is taken directly
 * from net_handle_misc_player_fields and HandleExpInfoResponse: `expinfo` is
 * always present and `expcard` is sent only for the expired branch. */
static u32 vm_net_mock_build_exp_card_status_response(const u8 *request,
                                                      u32 requestLen,
                                                      u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role;
    u32 multiplier = 1;
    u32 insightBonus = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    const char *info;
    bool active;

    if (!vm_net_mock_is_exp_card_status_request(request, requestLen) ||
        out == NULL || outCap < pos)
    {
        return 0;
    }

    role = vm_net_mock_active_role();
    multiplier = vm_net_mock_role_active_exp_card_multiplier(role);
    insightBonus = vm_net_mock_role_active_battle_exp_bonus_percent(role);
    active = multiplier > 1u || insightBonus != 0;
    if (multiplier > 1u)
    {
        info = vm_net_mock_exp_card_active_info(
            multiplier, vm_net_mock_role_active_exp_card_remaining_seconds(role));
    }
    else if (insightBonus != 0)
    {
        info = vm_net_mock_battle_insight_active_info(
            vm_net_mock_role_active_battle_insight_remaining_seconds(role));
    }
    else
    {
        info = "";
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 31, &objectStart) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "expinfo", info))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    if (!active)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 32, &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "expcard", 0))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
    }
    vm_net_mock_finish_wt_packet(out, pos, active ? 1 : 2);

    printf("[info][network] mock_exp_card_status multiplier=%u insight=%u active=%u response=%u evidence=JianghuOL.CBE:0x0100E3B8+0x01011A5E\n",
           multiplier, insightBonus, active ? 1u : 0u, pos);
    return pos;
}

/*
 * JianghuOL.CBE:0x01023630 sends exp cards as 1/7/30.  Its handler at
 * 0x01025AE6 removes the selected row itself only when result==1.  Likewise
 * category-21 pills use 1/22/3 and remove their selected row in that handler.
 * Therefore this builder must return exactly one matching object and must not
 * append the generic 7/1, 7/7, or 7/11 inventory mutation messages.
 */
static u32 vm_net_mock_build_timed_special_item_use_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u16 requestedSeq = 0;
    u8 requestKind = 0;
    u8 requestSubtype = 0;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *item = NULL;
    const vm_net_mock_item_effect_catalog_item *catalogItem = NULL;
    vm_net_mock_role_item_effect effect;
    u32 resolvedItemId = 0;
    u32 multiplier = 0;
    u32 durationSeconds = 0;
    u32 now = (u32)time(NULL);
    bool isExpCard = false;
    bool isBattleInsight = false;
    bool isCombatPill = false;
    bool success = false;
    const char *info = "item unavailable";
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 30,
                                                   "itemseq", true, &requestedSeq))
    {
        requestKind = 7;
        requestSubtype = 30;
        isExpCard = true;
    }
    else if (vm_net_mock_parse_special_item_seq_request(request, requestLen, 22, 3,
                                                        "seq", false, &requestedSeq))
    {
        requestKind = 22;
        requestSubtype = 3;
        isCombatPill = true;
    }
    else if (vm_net_mock_parse_special_item_seq_request(request, requestLen, 25, 6,
                                                        "seq", false, &requestedSeq))
    {
        requestKind = 25;
        requestSubtype = 6;
        isBattleInsight = true;
    }
    else
    {
        return 0;
    }

    memset(&effect, 0, sizeof(effect));
    role = vm_net_mock_active_role();
    if (role != NULL)
        item = vm_net_mock_role_find_backpack_item(role, 0, requestedSeq);
    if (item != NULL)
    {
        resolvedItemId = item->itemId;
        catalogItem = vm_net_mock_find_item_effect_catalog_item(item->itemId);
        multiplier = vm_net_mock_exp_card_multiplier_for_item(item->itemId);
        if (isExpCard)
        {
            effect.kind = VM_NET_MOCK_ROLE_ITEM_EFFECT_EXP_CARD;
            effect.itemId = item->itemId;
            effect.multiplier = multiplier;
            if (catalogItem != NULL && multiplier != 0 &&
                catalogItem->durationMinutes == 60 &&
                catalogItem->category == 10)
            {
                durationSeconds = (u32)catalogItem->durationMinutes * 60u;
            }
        }
        else if (isBattleInsight)
        {
            effect.kind = VM_NET_MOCK_ROLE_ITEM_EFFECT_BATTLE_INSIGHT;
            effect.itemId = item->itemId;
            /* item.dsh explicitly says battle experience +20%. */
            effect.multiplier = 20;
            if (catalogItem != NULL && item->itemId == 828 &&
                catalogItem->durationMinutes == 60 && catalogItem->category == 10)
            {
                durationSeconds = (u32)catalogItem->durationMinutes * 60u;
            }
        }
        else if (isCombatPill)
        {
            effect.kind = VM_NET_MOCK_ROLE_ITEM_EFFECT_COMBAT_PILL;
            effect.itemId = item->itemId;
            effect.multiplier = 0;
            if (catalogItem != NULL && (item->itemId == 829 || item->itemId == 830) &&
                catalogItem->durationMinutes == 30 && catalogItem->category == 21)
            {
                durationSeconds = (u32)catalogItem->durationMinutes * 60u;
            }
        }
        if (isCombatPill)
        {
            /*
             * item.dsh only says "明显/巨幅提升" with zero ATK/DEF columns.
             * Persist a documented percent interpretation in multiplier and
             * apply it on battle attack while the timed effect is active.
             * 829 大力丸 -> +30% ATK; 830 神力丸 -> +60% ATK.
             */
            if (item->itemId == 829)
                effect.multiplier = 30;
            else if (item->itemId == 830)
                effect.multiplier = 60;
            if (durationSeconds != 0 && now <= 0xffffffffu - durationSeconds &&
                effect.multiplier != 0)
            {
                const char *failInfo = NULL;
                effect.expiresUnix = now + durationSeconds;
                success = vm_net_mock_role_consume_backpack_item_with_timed_effect(
                    role, item->itemId, requestedSeq, &effect, NULL,
                    "combat-pill-use", &failInfo);
                info = success
                           ? "\xB4\xF3\xC1\xA6\xCD\xE2\xB9\xA5\xD2\xD1\xC9\xFA\xD0\xA7\xA1\xA3"
                           : (failInfo ? failInfo
                                       : "\xCD\xAC\xC0\xE0\xD0\xA7\xB9\xFB\xD2\xD1\xC9\xFA\xD0\xA7\xA3\xAC\xC7\xEB\xB5\xC8\xB4\xFD\xBD\xE1\xCA\xF8\xBA\xF3\xD4\xD9\xCA\xB9\xD3\xC3\xA1\xA3");
            }
            else
            {
                info = "\xB8\xC3\xB5\xC0\xBE\xDF\xB5\xC4\xC8\xA8\xCD\xFE\xB9\xA5\xB7\xC0\xCA\xFD\xD6\xB5\xC9\xD0\xCE\xB4\xC5\xE4\xD6\xC3\xA3\xAC\xCE\xB4\xCF\xFB\xBA\xC4\xA1\xA3";
            }
        }
        else if (durationSeconds != 0 && now <= 0xffffffffu - durationSeconds)
        {
            const char *failInfo = NULL;
            effect.expiresUnix = now + durationSeconds;
            success = vm_net_mock_role_consume_backpack_item_with_timed_effect(
                role, item->itemId, requestedSeq, &effect, NULL,
                isExpCard ? "exp-card-use" : "battle-insight-use", &failInfo);
            if (success)
            {
                if (isBattleInsight)
                {
                    /* Same remaining suffix as 7/31 tip so stack/use shows
                     * wall-clock left, not only the fixed +20% wording. */
                    info = vm_net_mock_battle_insight_use_success_info(
                        vm_net_mock_role_active_battle_insight_remaining_seconds(
                            role));
                }
                else
                {
                    info = vm_net_mock_special_item_success_info(resolvedItemId);
                }
            }
            else if (isExpCard)
                info = failInfo ? failInfo
                                : "\xBE\xAD\xD1\xE9\xBF\xA8\xCA\xB9\xD3\xC3\xCA\xA7\xB0\xDC\xA3\xAC\xC7\xEB\xC9\xD4\xBA\xF3\xD4\xD9\xCA\xD4\xA1\xA3"; /* 经验卡使用失败，请稍后再试。 */
            else
                info = failInfo ? failInfo
                                : "\xCD\xAC\xC0\xE0\xD0\xA7\xB9\xFB\xD2\xD1\xC9\xFA\xD0\xA7\xA3\xAC\xC7\xEB\xB5\xC8\xB4\xFD\xBD\xE1\xCA\xF8\xBA\xF3\xD4\xD9\xCA\xB9\xD3\xC3\xA1\xA3";
        }
    }

    if (requestKind == 7 && requestSubtype == 30)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 30, &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "result", success ? 1 : 0) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "iteminfo", info))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        /* 7/31 expinfo drives the top-left icon (0x01011A5E); keep 7/30 alone
         * for inventory mutation and append status only after success. */
        if (success)
        {
            if (!vm_net_mock_append_exp_card_info_object(out, outCap, &pos))
                return 0;
            vm_net_mock_finish_wt_packet(out, pos, 2);
        }
        else
        {
            vm_net_mock_finish_wt_packet(out, pos, 1);
        }
    }
    else if (requestKind == 25 && requestSubtype == 6)
    {
        u8 statusObjects = 0;

        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 25, 6, &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "result", success ? 1 : 2) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "maxnum", 0) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "iteminfo", info))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        /* 25/6 does not write local expcard (unlike 7/30).  Arm the shared
         * top-left status icon the same way custom exp cards do after 7/1. */
        if (success)
        {
            if (!vm_net_mock_append_status_icon_arm_objects(out, outCap, &pos,
                                                            &statusObjects))
                return 0;
            vm_net_mock_finish_wt_packet(out, pos, (u8)(1u + statusObjects));
        }
        else
        {
            vm_net_mock_finish_wt_packet(out, pos, 1);
        }
    }
    else
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 22, 3, &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "result", success ? 1 : 4) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "info", info) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "ruffianflag", 0))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        vm_net_mock_finish_wt_packet(out, pos, 1);
    }

    printf("[info][network] mock_special_item_use request=%u/%u item=%u seq=%u kind=%u multiplier=%u duration=%u success=%u response=%u evidence=JianghuOL.CBE:0x01023630+0x01025AE6,item.dsh\n",
           requestKind, requestSubtype, resolvedItemId, requestedSeq,
           effect.kind, effect.multiplier, durationSeconds,
           success ? 1u : 0u, pos);
    vm_autotest_note("mock_special_item_use request=%u/%u item=%u seq=%u kind=%u multiplier=%u duration=%u success=%u response=%u evidence=JianghuOL.CBE:0x01023630+0x01025AE6,item.dsh\n",
                     requestKind, requestSubtype, resolvedItemId,
                     requestedSeq, effect.kind, effect.multiplier,
                     durationSeconds, success ? 1u : 0u, pos);
    return pos;
}

/*
 * After a successful 1/25/6 battle-insight use, JianghuOL.CBE:0x0102DC40 builds
 * follow-up 1/25/7 {num:u8=1,seq:u16}. Parser 0x0102DD7A mirrors the 25/6
 * shape: result (cmp 1 success / 2 fail), maxnum (i16), iteminfo (string).
 * result=1 clears the client wait at 0x0101095e; result=2 surfaces the nearby
 * "功能暂未开放!" path. This is not a second consume — 25/6 already applied
 * the timed effect and removed the backpack row.
 */
static u32 vm_net_mock_build_battle_insight_followup_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u16 requestedSeq = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 statusObjects = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_parse_special_item_seq_request(request, requestLen, 25, 7,
                                                    "seq", true, &requestedSeq))
    {
        return 0;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 25, 7, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "maxnum", 0) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "iteminfo", ""))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    /* Re-arm icon after wait-clear in case 25/6 companions were ignored. */
    if (!vm_net_mock_append_status_icon_arm_objects(out, outCap, &pos,
                                                    &statusObjects))
        return 0;
    vm_net_mock_finish_wt_packet(out, pos, (u8)(1u + statusObjects));

    printf("[info][network] mock_battle_insight_followup request=25/7 seq=%u response=%u status_objs=%u evidence=JianghuOL.CBE:0x0102DC40+0x0102DD7A+0x01011AF8\n",
           requestedSeq, pos, statusObjects);
    vm_autotest_note("mock_battle_insight_followup request=25/7 seq=%u response=%u evidence=JianghuOL.CBE:0x0102DC40+0x0102DD7A\n",
                     requestedSeq, pos);
    return pos;
}

/* These requests have parser-proven response contracts, but their durable
 * gameplay state is not yet represented by an authoritative server record:
 * 833 needs vitality, and 920/921 need the book instance's level and
 * experience payload.  827 修炼丹 is handled by
 * vm_net_mock_build_practise_pill_use_response.  Return a parser-valid
 * non-success response instead of silently dropping the request or consuming
 * an item with no corresponding gameplay effect. */
static u32 vm_net_mock_build_unresolved_special_item_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u16 requestedSeq = 0;
    u8 subtype = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    const char *itemInfo =
        "\xB8\xC3\xB5\xC0\xBE\xDF\xB5\xC4\xC8\xA8\xCD\xFE\xD7\xB4\xCC\xAC\xC9\xD0\xCE\xB4\xC5\xE4\xD6\xC3\xA3\xAC\xCE\xB4\xCF\xFB\xBA\xC4\xA1\xA3";
    const char *bookInfo =
        "\xD0\xDE\xC1\xB6\xCC\xEC\xCA\xE9\xD7\xCA\xC1\xCF\xC9\xD0\xCE\xB4\xC5\xE4\xD6\xC3\xA3\xAC\xCE\xB4\xCF\xFB\xBA\xC4\xA1\xA3";

    if (out == NULL || outCap < pos)
        return 0;
    if (vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 33,
                                                        "itemseq", false, &requestedSeq))
    {
        subtype = 33;
    }
    else if (vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 35,
                                                        "seq", false, &requestedSeq))
    {
        subtype = 35;
    }
    else if (vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 38,
                                                        "seq", false, &requestedSeq))
    {
        subtype = 38;
    }
    else if (vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 40,
                                                        "seq", false, &requestedSeq))
    {
        subtype = 40;
    }
    else
    {
        return 0;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, subtype, &objectStart))
        return 0;
    if (subtype == 33)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 2) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "maxnum", 0) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "iteminfo", itemInfo))
        {
            return 0;
        }
    }
    else if (subtype == 35)
    {
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "bookdes", bookInfo))
        {
            return 0;
        }
    }
    else if (subtype == 38)
    {
        if (!vm_net_mock_put_object_string(out, outCap, &pos, "bookdes", bookInfo))
            return 0;
    }
    else
    {
        /* result=1 displays bookinfo without removing the selected 921 row. */
        if (!vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "bookinfo", bookInfo))
        {
            return 0;
        }
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[warn][network] mock_special_item_unresolved request=7/%u seq=%u action=not-consumed response=%u evidence=JianghuOL.CBE:0x01025AE6\n",
           subtype, requestedSeq, pos);
    return pos;
}

/*
 * JianghuOL.CBE uses 1/7/16 for 修炼丹.  Success result=1 lets the client remove
 * one stack itself; server adds +60 minutes into the offline-practise bank
 * (item.dsh: 1h each, bank cap 100h).
 */
static u32 vm_net_mock_build_practise_pill_use_response(const u8 *request,
                                                        u32 requestLen,
                                                        u8 *out, u32 outCap)
{
    u16 requestedSeq = 0;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *item = NULL;
    bool success = false;
    u32 remaining = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    const char *info =
        "\xD0\xDE\xC1\xB6\xB5\xA4\xCA\xB9\xD3\xC3\xCA\xA7\xB0\xDC\xA3\xAC\xC7\xEB\xC9\xD4\xBA\xF3\xD4\xD9\xCA\xD4\xA1\xA3";

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 16,
                                                    "itemseq", false, &requestedSeq))
    {
        return 0;
    }

    role = vm_net_mock_active_role();
    if (role != NULL)
        item = vm_net_mock_role_find_backpack_item(
            role, VM_NET_MOCK_PRACTISE_PILL_ITEM_ID, requestedSeq);
    if (item != NULL && item->itemId == VM_NET_MOCK_PRACTISE_PILL_ITEM_ID)
    {
        success = vm_net_mock_role_use_practise_pill(role, requestedSeq, &remaining);
        if (success)
        {
            info =
                "\xBB\xF1\xB5\xC3\x31\xD0\xA1\xCA\xB1\xD0\xDE\xC1\xB6\xCA\xB1\xBC\xE4"
                "\xA3\xAC\xC0\xEB\xCF\xDF\xBA\xF3\xD7\xD4\xB6\xAF\xBF\xAA\xCA\xBC"
                "\xD0\xDE\xC1\xB6\xA1\xA3";
        }
        else
        {
            info =
                "\xD0\xDE\xC1\xB6\xCA\xB1\xBC\xE4\xD2\xD1\xB4\xEF\xC0\xDB\xBC\xC6"
                "\xC9\xCF\xCF\xDE\xBB\xF2\xCA\xB9\xD3\xC3\xCA\xA7\xB0\xDC\xA1\xA3";
        }
    }
    else
    {
        info =
            "\xCE\xB4\xD5\xD2\xB5\xBD\xD0\xDE\xC1\xB6\xB5\xA4\xA1\xA3";
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 16, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", success ? 1 : 2) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "maxnum", remaining) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "iteminfo", info))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_practise_pill_use seq=%u success=%u remaining=%u response=%u evidence=JianghuOL.CBE:0x01025AE6,item.dsh:827\n",
           requestedSeq, success ? 1u : 0u, remaining, pos);
    return pos;
}

/*
 * Runtime (2026-07-27): after successful 7/16, client immediately sends
 * wt=7/17 len=38 (payload ~29).  Nearby CBE literals pair `itemseq` with
 * `num` the same way 25/7 follows 25/6.  Empty ignored-unhandled leaves the
 * client wait stuck.  Reply 7/17 {result=1,maxnum=0,iteminfo=""} and do not
 * consume again — bank minutes were already added in 7/16.
 */
static u32 vm_net_mock_build_practise_pill_followup_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u16 requestedSeq = 0;
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 17,
                                                    "itemseq", true, &requestedSeq) &&
        !vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 17,
                                                    "itemseq", false, &requestedSeq) &&
        !vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 17,
                                                    "seq", true, &requestedSeq))
    {
        return 0;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 17, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "maxnum", 0) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "iteminfo", ""))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_practise_pill_followup request=7/17 seq=%u response=%u evidence=runtime:wt=7/17-after-7/16+JianghuOL.CBE:itemseq/num\n",
           requestedSeq, pos);
    return pos;
}

/*
 * JianghuOL.CBE:0x01023706 builds open-chest as 1/7/15 {box:u16,key:u16}.
 * Handler 0x010261EC accepts only kind=7/subtype=15 and branches on result:
 *   1=reward via total+info, 2=no box, 3=no key, 4=bag full, 5=gold, 6=fail.
 * On result=1 the client removes the selected chest/key pair itself
 * (0x01023850), so this builder must not append generic 7/7 or 7/11.
 *
 * Reward pools:
 *   - Preferred: MySQL server_chest_rewards (admin 宝箱奖励), weighted pick.
 *   - Fallback for gold 524 only: legacy hardcoded eligibility over shop
 *     catalog when that chest has no enabled DB rows.
 */
enum
{
    VM_NET_MOCK_BRONZE_CHEST_ITEM_ID = 522,
    VM_NET_MOCK_SILVER_CHEST_ITEM_ID = 523,
    VM_NET_MOCK_GOLD_CHEST_ITEM_ID = 524,
    VM_NET_MOCK_BRONZE_CHEST_KEY_ID = 813,
    VM_NET_MOCK_SILVER_CHEST_KEY_ID = 814,
    VM_NET_MOCK_GOLD_CHEST_KEY_ID = 815,
    VM_NET_MOCK_CHEST_REWARD_MAX_ROWS = 4096
};

typedef struct
{
    u32 chestItemId;
    u32 rewardItemId;
    u32 weight;
    u8 enabled;
} vm_net_mock_chest_reward_row;

static u32 g_vm_net_mock_chest_open_rng = 0;
static vm_net_mock_chest_reward_row
    g_vm_net_mock_chest_rewards[VM_NET_MOCK_CHEST_REWARD_MAX_ROWS];
static u32 g_vm_net_mock_chest_reward_count = 0;
static bool g_vm_net_mock_chest_reward_db_loaded = false;
static bool g_vm_net_mock_chest_reward_db_valid = false;

static u32 vm_net_mock_chest_key_for_box(u32 boxItemId)
{
    switch (boxItemId)
    {
    case VM_NET_MOCK_BRONZE_CHEST_ITEM_ID:
        return VM_NET_MOCK_BRONZE_CHEST_KEY_ID;
    case VM_NET_MOCK_SILVER_CHEST_ITEM_ID:
        return VM_NET_MOCK_SILVER_CHEST_KEY_ID;
    case VM_NET_MOCK_GOLD_CHEST_ITEM_ID:
        return VM_NET_MOCK_GOLD_CHEST_KEY_ID;
    default:
        return 0;
    }
}

static bool vm_net_mock_chest_box_is_supported(u32 boxItemId)
{
    return vm_net_mock_chest_key_for_box(boxItemId) != 0;
}

static const char *vm_net_mock_chest_box_label_utf8(u32 boxItemId)
{
    switch (boxItemId)
    {
    case VM_NET_MOCK_BRONZE_CHEST_ITEM_ID:
        return "青铜宝箱";
    case VM_NET_MOCK_SILVER_CHEST_ITEM_ID:
        return "白银宝箱";
    case VM_NET_MOCK_GOLD_CHEST_ITEM_ID:
        return "黄金宝箱";
    default:
        return "宝箱";
    }
}

static u32 vm_net_mock_chest_open_rand(void)
{
    if (g_vm_net_mock_chest_open_rng == 0)
    {
        const vm_net_mock_role_state *role = vm_net_mock_active_role();
        g_vm_net_mock_chest_open_rng =
            0xa5a5a5a5u ^
            (g_schedulerTick * 1664525u) ^
            ((u32)time(NULL) * 1013904223u) ^
            (role != NULL ? (role->roleId * 747796405u) : 0u);
        if (g_vm_net_mock_chest_open_rng == 0)
            g_vm_net_mock_chest_open_rng = 0x9e3779b9u;
    }
    g_vm_net_mock_chest_open_rng ^= g_vm_net_mock_chest_open_rng << 13;
    g_vm_net_mock_chest_open_rng ^= g_vm_net_mock_chest_open_rng >> 17;
    g_vm_net_mock_chest_open_rng ^= g_vm_net_mock_chest_open_rng << 5;
    return g_vm_net_mock_chest_open_rng;
}

/* Legacy gold-only default when MySQL pool for 524 is empty. */
static bool vm_net_mock_chest_reward_item_eligible_legacy(u32 itemId)
{
    const vm_net_mock_shop_catalog_item *shop = NULL;
    const vm_net_mock_equipment_catalog_item *equip = NULL;

    if (itemId == 0)
        return false;
    shop = vm_net_mock_find_shop_catalog_item(itemId);
    if (shop == NULL)
        return false;

    if (shop->isEquip)
    {
        equip = vm_net_mock_find_equipment_catalog_item(itemId);
        return equip != NULL && equip->quality >= 1 && equip->quality <= 3;
    }
    if (shop->category == 14)
        return true;
    if (shop->category == 10 && itemId != 920 && itemId != 921)
        return true;
    if (shop->category == 21 || shop->category == 23 || shop->category == 27)
        return true;
    if (itemId == 835 || itemId == 836 || itemId == 837)
        return true;
    return false;
}

typedef struct
{
    u32 loaded;
    u32 skipped;
} vm_net_mock_chest_reward_load_context;

static bool vm_net_mock_chest_reward_db_row(void *contextValue,
                                            unsigned int columnCount,
                                            const char *const *values,
                                            const size_t *lengths)
{
    vm_net_mock_chest_reward_load_context *context =
        (vm_net_mock_chest_reward_load_context *)contextValue;
    u32 chestItemId = 0;
    u32 rewardItemId = 0;
    u32 weight = 1;
    u32 enabled = 1;
    vm_net_mock_chest_reward_row *dst = NULL;

    if (context == NULL || values == NULL || lengths == NULL || columnCount < 4)
        return false;
    if (g_vm_net_mock_chest_reward_count >= VM_NET_MOCK_CHEST_REWARD_MAX_ROWS)
    {
        ++context->skipped;
        return true;
    }
    if (!vm_mock_mysql_parse_u32(values[0], lengths[0], &chestItemId) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &rewardItemId) ||
        chestItemId == 0 || rewardItemId == 0 ||
        !vm_net_mock_chest_box_is_supported(chestItemId))
    {
        ++context->skipped;
        return true;
    }
    if (lengths[2] != 0)
        (void)vm_mock_mysql_parse_u32(values[2], lengths[2], &weight);
    if (lengths[3] != 0)
        (void)vm_mock_mysql_parse_u32(values[3], lengths[3], &enabled);
    if (weight == 0)
        weight = 1;
    if (enabled > 1)
    {
        ++context->skipped;
        return true;
    }
    dst = &g_vm_net_mock_chest_rewards[g_vm_net_mock_chest_reward_count++];
    memset(dst, 0, sizeof(*dst));
    dst->chestItemId = chestItemId;
    dst->rewardItemId = rewardItemId;
    dst->weight = weight;
    dst->enabled = enabled != 0 ? 1 : 0;
    ++context->loaded;
    return true;
}

static bool vm_net_mock_chest_reward_db_load(void)
{
    vm_net_mock_chest_reward_load_context context;

    if (g_vm_net_mock_chest_reward_db_loaded)
        return g_vm_net_mock_chest_reward_db_valid;
    g_vm_net_mock_chest_reward_db_loaded = true;
    g_vm_net_mock_chest_reward_db_valid = false;
    g_vm_net_mock_chest_reward_count = 0;
    memset(&context, 0, sizeof(context));

    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_chest_rewards ("
            "chest_item_id INT UNSIGNED NOT NULL,"
            "reward_item_id INT UNSIGNED NOT NULL,"
            "weight INT UNSIGNED NOT NULL DEFAULT 1,"
            "enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP "
            "ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(chest_item_id,reward_item_id)"
            ") ENGINE=InnoDB") ||
        !vm_mysql_query(
            "SELECT chest_item_id,reward_item_id,weight,enabled "
            "FROM server_chest_rewards ORDER BY chest_item_id,reward_item_id",
            vm_net_mock_chest_reward_db_row, &context))
    {
        printf("[error][mock-admin] chest_reward_db_load failed error=%s\n",
               vm_mysql_last_error());
        return false;
    }
    g_vm_net_mock_chest_reward_db_valid = true;
    printf("[info][mock-admin] chest_reward_db_load rows=%u skipped=%u\n",
           context.loaded, context.skipped);
    return true;
}

static bool vm_net_mock_chest_reward_admin_reload(const char **errorOut)
{
    g_vm_net_mock_chest_reward_db_loaded = false;
    g_vm_net_mock_chest_reward_db_valid = false;
    g_vm_net_mock_chest_reward_count = 0;
    if (!vm_net_mock_chest_reward_db_load())
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (errorOut)
        *errorOut = "ok";
    return true;
}

static u32 vm_net_mock_chest_reward_admin_count(u32 chestItemId)
{
    u32 count = 0;

    (void)vm_net_mock_chest_reward_db_load();
    for (u32 i = 0; i < g_vm_net_mock_chest_reward_count; ++i)
    {
        if (g_vm_net_mock_chest_rewards[i].chestItemId == chestItemId)
            ++count;
    }
    return count;
}

static u32 vm_net_mock_chest_reward_admin_enabled_count(u32 chestItemId)
{
    u32 count = 0;

    (void)vm_net_mock_chest_reward_db_load();
    for (u32 i = 0; i < g_vm_net_mock_chest_reward_count; ++i)
    {
        if (g_vm_net_mock_chest_rewards[i].chestItemId == chestItemId &&
            g_vm_net_mock_chest_rewards[i].enabled != 0)
            ++count;
    }
    return count;
}

static const vm_net_mock_chest_reward_row *vm_net_mock_chest_reward_admin_row_at(
    u32 chestItemId, u32 ordinal)
{
    u32 matched = 0;

    (void)vm_net_mock_chest_reward_db_load();
    for (u32 i = 0; i < g_vm_net_mock_chest_reward_count; ++i)
    {
        if (g_vm_net_mock_chest_rewards[i].chestItemId != chestItemId)
            continue;
        if (matched == ordinal)
            return &g_vm_net_mock_chest_rewards[i];
        ++matched;
    }
    return NULL;
}

static bool vm_net_mock_chest_reward_admin_upsert(u32 chestItemId,
                                                  u32 rewardItemId,
                                                  u32 weight,
                                                  bool enabled,
                                                  const char **errorOut)
{
    char query[512];

    if (errorOut)
        *errorOut = "宝箱奖励参数无效";
    if (!vm_net_mock_chest_box_is_supported(chestItemId) || rewardItemId == 0)
        return false;
    if (vm_net_mock_find_shop_catalog_item(rewardItemId) == NULL)
    {
        if (errorOut)
            *errorOut = "奖励物品不在商品目录中";
        return false;
    }
    if (weight == 0)
        weight = 1;
    if (!g_vm_net_mock_chest_reward_db_valid &&
        !vm_net_mock_chest_reward_admin_reload(errorOut))
        return false;

    snprintf(query, sizeof(query),
             "INSERT INTO server_chest_rewards"
             "(chest_item_id,reward_item_id,weight,enabled) "
             "VALUES(%u,%u,%u,%u) "
             "ON DUPLICATE KEY UPDATE weight=VALUES(weight),"
             "enabled=VALUES(enabled)",
             chestItemId, rewardItemId, weight, enabled ? 1u : 0u);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (!vm_net_mock_chest_reward_admin_reload(errorOut))
        return false;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] chest_reward_save chest=%u reward=%u weight=%u "
           "enabled=%u\n",
           chestItemId, rewardItemId, weight, enabled ? 1u : 0u);
    return true;
}

static bool vm_net_mock_chest_reward_admin_delete(u32 chestItemId,
                                                  u32 rewardItemId,
                                                  const char **errorOut)
{
    char query[256];

    if (errorOut)
        *errorOut = "宝箱奖励参数无效";
    if (!vm_net_mock_chest_box_is_supported(chestItemId) || rewardItemId == 0)
        return false;
    if (!g_vm_net_mock_chest_reward_db_valid &&
        !vm_net_mock_chest_reward_admin_reload(errorOut))
        return false;
    snprintf(query, sizeof(query),
             "DELETE FROM server_chest_rewards WHERE chest_item_id=%u AND "
             "reward_item_id=%u",
             chestItemId, rewardItemId);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (!vm_net_mock_chest_reward_admin_reload(errorOut))
        return false;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] chest_reward_delete chest=%u reward=%u\n",
           chestItemId, rewardItemId);
    return true;
}

static bool vm_net_mock_chest_reward_admin_clear(u32 chestItemId,
                                                 const char **errorOut)
{
    char query[192];

    if (errorOut)
        *errorOut = "宝箱参数无效";
    if (!vm_net_mock_chest_box_is_supported(chestItemId))
        return false;
    if (!g_vm_net_mock_chest_reward_db_valid &&
        !vm_net_mock_chest_reward_admin_reload(errorOut))
        return false;
    snprintf(query, sizeof(query),
             "DELETE FROM server_chest_rewards WHERE chest_item_id=%u",
             chestItemId);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    if (!vm_net_mock_chest_reward_admin_reload(errorOut))
        return false;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] chest_reward_clear chest=%u\n", chestItemId);
    return true;
}

/*
 * Batch enable/disable rows already in one chest pool.
 * matchCategory: shop.category + isEquip must match.
 * matchQuality: equipment rows whose equip.dsh 品质 equals quality.
 * Both filters may be combined (AND).
 */
static bool vm_net_mock_chest_reward_admin_batch_set_enabled(
    u32 chestItemId,
    bool matchCategory,
    bool categoryIsEquip,
    u8 category,
    bool matchQuality,
    u8 quality,
    bool enabled,
    u32 *changedOut,
    const char **errorOut)
{
    char query[320];
    u32 changed = 0;

    if (changedOut)
        *changedOut = 0;
    if (errorOut)
        *errorOut = "宝箱批量参数无效";
    if (!vm_net_mock_chest_box_is_supported(chestItemId))
        return false;
    if (!matchCategory && !matchQuality)
    {
        if (errorOut)
            *errorOut = "请指定物品分类或装备品质";
        return false;
    }
    (void)vm_net_mock_load_shop_catalog();
    (void)vm_net_mock_load_equipment_catalog();
    if (!g_vm_net_mock_chest_reward_db_valid &&
        !vm_net_mock_chest_reward_admin_reload(errorOut))
        return false;

    for (u32 i = 0; i < g_vm_net_mock_chest_reward_count; ++i)
    {
        const vm_net_mock_chest_reward_row *row = &g_vm_net_mock_chest_rewards[i];
        const vm_net_mock_shop_catalog_item *shop = NULL;
        const vm_net_mock_equipment_catalog_item *equip = NULL;
        bool ok = true;

        if (row->chestItemId != chestItemId)
            continue;
        shop = vm_net_mock_find_shop_catalog_item(row->rewardItemId);
        if (shop == NULL)
            continue;
        if (matchCategory)
        {
            if ((shop->isEquip != 0) != categoryIsEquip ||
                shop->category != category)
                ok = false;
        }
        if (ok && matchQuality)
        {
            if (shop->isEquip == 0)
                ok = false;
            else
            {
                equip = vm_net_mock_find_equipment_catalog_item(row->rewardItemId);
                if (equip == NULL || equip->quality != quality)
                    ok = false;
            }
        }
        if (!ok)
            continue;
        if ((row->enabled != 0) == enabled)
            continue;

        snprintf(query, sizeof(query),
                 "UPDATE server_chest_rewards SET enabled=%u "
                 "WHERE chest_item_id=%u AND reward_item_id=%u",
                 enabled ? 1u : 0u, chestItemId, row->rewardItemId);
        if (!vm_mysql_exec(query))
        {
            if (errorOut)
                *errorOut = vm_mysql_last_error();
            return false;
        }
        ++changed;
    }

    if (changed != 0 && !vm_net_mock_chest_reward_admin_reload(errorOut))
        return false;
    if (changedOut)
        *changedOut = changed;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] chest_reward_batch chest=%u enabled=%u "
           "cat_filter=%u equip=%u cat=%u q_filter=%u q=%u changed=%u\n",
           chestItemId, enabled ? 1u : 0u, matchCategory ? 1u : 0u,
           categoryIsEquip ? 1u : 0u, category, matchQuality ? 1u : 0u, quality,
           changed);
    return true;
}

/*
 * Seed one chest from the legacy gold default rule set (quality 1-3 + cats).
 * Used once to bootstrap admin-editable pools for gold/silver/bronze.
 */
static bool vm_net_mock_chest_reward_admin_import_legacy_defaults(
    u32 chestItemId, const char **errorOut)
{
    u32 total = 0;
    u32 imported = 0;
    char query[512];

    if (errorOut)
        *errorOut = "宝箱参数无效";
    if (!vm_net_mock_chest_box_is_supported(chestItemId))
        return false;
    if (!g_vm_net_mock_chest_reward_db_valid &&
        !vm_net_mock_chest_reward_admin_reload(errorOut))
        return false;

    total = vm_net_mock_load_shop_catalog();
    for (u32 i = 0; i < total; ++i)
    {
        u32 itemId = g_vm_net_mock_shop_catalog[i].itemId;
        if (!vm_net_mock_chest_reward_item_eligible_legacy(itemId))
            continue;
        snprintf(query, sizeof(query),
                 "INSERT INTO server_chest_rewards"
                 "(chest_item_id,reward_item_id,weight,enabled) "
                 "VALUES(%u,%u,1,1) "
                 "ON DUPLICATE KEY UPDATE weight=VALUES(weight),"
                 "enabled=VALUES(enabled)",
                 chestItemId, itemId);
        if (!vm_mysql_exec(query))
        {
            if (errorOut)
                *errorOut = vm_mysql_last_error();
            return false;
        }
        ++imported;
    }
    if (!vm_net_mock_chest_reward_admin_reload(errorOut))
        return false;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] chest_reward_import_legacy chest=%u imported=%u\n",
           chestItemId, imported);
    return true;
}

static bool vm_net_mock_chest_roll_reward_from_db(u32 chestItemId,
                                                  u32 *itemIdOut)
{
    u64 totalWeight = 0;
    u64 pick = 0;
    u32 bound = 0;

    if (itemIdOut)
        *itemIdOut = 0;
    (void)vm_net_mock_chest_reward_db_load();
    for (u32 i = 0; i < g_vm_net_mock_chest_reward_count; ++i)
    {
        const vm_net_mock_chest_reward_row *row = &g_vm_net_mock_chest_rewards[i];
        if (row->chestItemId != chestItemId || row->enabled == 0 ||
            row->weight == 0)
            continue;
        totalWeight += row->weight;
    }
    if (totalWeight == 0)
        return false;
    bound = totalWeight > 0xffffffffu ? 0xffffffffu : (u32)totalWeight;
    pick = vm_net_mock_chest_open_rand() % bound;
    for (u32 i = 0; i < g_vm_net_mock_chest_reward_count; ++i)
    {
        const vm_net_mock_chest_reward_row *row = &g_vm_net_mock_chest_rewards[i];
        if (row->chestItemId != chestItemId || row->enabled == 0 ||
            row->weight == 0)
            continue;
        if (pick < row->weight)
        {
            if (itemIdOut)
                *itemIdOut = row->rewardItemId;
            return true;
        }
        pick -= row->weight;
    }
    return false;
}

static bool vm_net_mock_chest_roll_reward_legacy_gold(u32 *itemIdOut)
{
    u32 total = 0;
    u32 count = 0;
    u32 pick = 0;

    if (itemIdOut)
        *itemIdOut = 0;

    total = vm_net_mock_load_shop_catalog();
    for (u32 i = 0; i < total; ++i)
    {
        if (vm_net_mock_chest_reward_item_eligible_legacy(
                g_vm_net_mock_shop_catalog[i].itemId))
            ++count;
    }
    if (count == 0)
        return false;

    pick = vm_net_mock_chest_open_rand() % count;
    for (u32 i = 0; i < total; ++i)
    {
        u32 itemId = g_vm_net_mock_shop_catalog[i].itemId;
        if (!vm_net_mock_chest_reward_item_eligible_legacy(itemId))
            continue;
        if (pick == 0)
        {
            if (itemIdOut)
                *itemIdOut = itemId;
            return true;
        }
        --pick;
    }
    return false;
}

static bool vm_net_mock_chest_roll_reward_item(u32 chestItemId, u32 *itemIdOut)
{
    if (itemIdOut)
        *itemIdOut = 0;
    if (!vm_net_mock_chest_box_is_supported(chestItemId))
        return false;
    if (vm_net_mock_chest_roll_reward_from_db(chestItemId, itemIdOut))
        return true;
    /* Gold keeps the old built-in pool until admin imports/configures DB rows. */
    if (chestItemId == VM_NET_MOCK_GOLD_CHEST_ITEM_ID)
        return vm_net_mock_chest_roll_reward_legacy_gold(itemIdOut);
    return false;
}

static u32 vm_net_mock_build_chest_open_result_response(u8 *out, u32 outCap,
                                                        u8 result)
{
    u32 pos = 5;
    u32 objectStart = 0;

    if (out == NULL || outCap < pos)
        return 0;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 15, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    return pos;
}

/*
 * JianghuOL.CBE:0x010237e0 / 0x010237ee put the chest/key sequences with ADR
 * into the literal pool `box\0key`.  Thumb ADR immediates are multiples of 4,
 * so both PC-relative loads land on the mid-string `x\0` rather than distinct
 * `box` / `key` names.  Live requests therefore carry two i16 entries named
 * `x` in box-then-key wire order.  Accept that shape; also keep canonical
 * `box`/`key` for hand-built packets.
 */
static bool vm_net_mock_chest_open_field_is_seq_name(const u8 *name, u8 nameLen)
{
    if (name == NULL || nameLen == 0)
        return false;
    if (nameLen == 3 && memcmp(name, "box", 3) == 0)
        return true;
    if (nameLen == 3 && memcmp(name, "key", 3) == 0)
        return true;
    if (nameLen == 1 && name[0] == 'x')
        return true;
    return false;
}

static bool vm_net_mock_chest_open_decode_seq_entry(const u8 *entry, u16 entryLen,
                                                    u32 *valueOut)
{
    u32 value = 0;

    if (valueOut)
        *valueOut = 0;
    if (entry == NULL || entryLen < 3 || entry[0] != 0)
        return false;
    switch (entry[1])
    {
    case 1:
        if (entryLen != 3)
            return false;
        value = entry[2];
        break;
    case 2:
        if (entryLen != 4)
            return false;
        value = ((u32)entry[2] << 8) | entry[3];
        break;
    case 4:
        if (entryLen != 6)
            return false;
        value = ((u32)entry[2] << 24) | ((u32)entry[3] << 16) |
                ((u32)entry[4] << 8) | entry[5];
        break;
    default:
        return false;
    }
    if (valueOut)
        *valueOut = value;
    return true;
}

static bool vm_net_mock_request_is_chest_open(const u8 *request, u32 requestLen,
                                              vm_net_mock_request_object *objectOut)
{
    u32 offset = 4;
    vm_net_mock_request_object object;

    if (objectOut)
        memset(objectOut, 0, sizeof(*objectOut));
    if (request == NULL || requestLen < 9 || request[0] != 'W' || request[1] != 'T')
        return false;
    if (!vm_net_mock_next_request_object(request, requestLen, &offset, &object) ||
        offset != requestLen || object.major != 1 || object.kind != 7 ||
        object.subtype != 15)
    {
        return false;
    }
    if (objectOut)
        *objectOut = object;
    return true;
}

static bool vm_net_mock_parse_chest_open_request(const u8 *request, u32 requestLen,
                                                 u16 *boxSeqOut, u16 *keySeqOut)
{
    vm_net_mock_request_object object;
    u32 boxSeq = 0;
    u32 keySeq = 0;
    u32 seqs[2];
    u32 seqCount = 0;
    u32 pos = 0;

    if (boxSeqOut)
        *boxSeqOut = 0;
    if (keySeqOut)
        *keySeqOut = 0;
    if (!vm_net_mock_request_is_chest_open(request, requestLen, &object))
        return false;

    /*
     * Prefer distinct names when present (tests / future client builds), then
     * fall back to wire-order collection of box|key|x — required for the live
     * dual-`x` ADR encoding.
     */
    if ((vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "box", &boxSeq) ||
         vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                              "x", &boxSeq)) &&
        vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                            "key", &keySeq) &&
        boxSeq != 0 && boxSeq <= 0xffffu && keySeq != 0 && keySeq <= 0xffffu)
    {
        if (boxSeqOut)
            *boxSeqOut = (u16)boxSeq;
        if (keySeqOut)
            *keySeqOut = (u16)keySeq;
        return true;
    }

    memset(seqs, 0, sizeof(seqs));
    while (pos < object.payloadLen && seqCount < 2)
    {
        u8 nameLen = 0;
        u16 valueLen = 0;
        const u8 *name = NULL;
        const u8 *value = NULL;
        u32 seq = 0;

        if (pos >= object.payloadLen)
            break;
        nameLen = object.payload[pos++];
        if (nameLen == 0 || pos + nameLen + 2 > object.payloadLen)
            return false;
        name = object.payload + pos;
        pos += nameLen;
        valueLen = (u16)(((u16)object.payload[pos] << 8) | object.payload[pos + 1]);
        pos += 2;
        if (pos + valueLen > object.payloadLen)
            return false;
        value = object.payload + pos;
        pos += valueLen;
        if (!vm_net_mock_chest_open_field_is_seq_name(name, nameLen))
            continue;
        if (!vm_net_mock_chest_open_decode_seq_entry(value, valueLen, &seq) ||
            seq == 0 || seq > 0xffffu)
        {
            return false;
        }
        seqs[seqCount++] = seq;
    }
    if (seqCount != 2)
        return false;
    if (boxSeqOut)
        *boxSeqOut = (u16)seqs[0];
    if (keySeqOut)
        *keySeqOut = (u16)seqs[1];
    return true;
}

static u32 vm_net_mock_build_chest_open_response(const u8 *request,
                                                 u32 requestLen,
                                                 u8 *out, u32 outCap)
{
    u16 boxSeq = 0;
    u16 keySeq = 0;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_role_state before;
    vm_net_mock_backpack_item_state *boxItem = NULL;
    vm_net_mock_backpack_item_state *keyItem = NULL;
    u32 rewardItemId = 0;
    u16 rewardSeq = 0;
    u32 rewardCount = 0;
    u32 chestItemId = 0;
    u32 keyItemId = 0;
    u8 itemInfo[VM_NET_MOCK_ITEM_USE_ITEMINFO_SCRATCH];
    u32 itemInfoLen = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 result = 6;

    if (out == NULL || outCap < pos)
        return 0;
    /* Recognized 7/15 must never return 0 — that leaves the open UI waiting. */
    if (!vm_net_mock_request_is_chest_open(request, requestLen, NULL))
        return 0;
    if (!vm_net_mock_parse_chest_open_request(request, requestLen, &boxSeq, &keySeq))
    {
        pos = vm_net_mock_build_chest_open_result_response(out, outCap, 6);
        printf("[warn][network] mock_chest_open result=6 reason=box-key-parse-failed "
               "resp=%u evidence=JianghuOL.CBE:0x010237e0-dual-x-ADR\n",
               pos);
        return pos;
    }

    role = vm_net_mock_active_role();
    if (role == NULL)
        return vm_net_mock_build_chest_open_result_response(out, outCap, 6);

    boxItem = vm_net_mock_role_find_backpack_item(role, 0, boxSeq);
    keyItem = vm_net_mock_role_find_backpack_item(role, 0, keySeq);
    if (boxItem == NULL || boxItem->count == 0 ||
        !vm_net_mock_chest_box_is_supported(boxItem->itemId))
    {
        result = 2;
        pos = vm_net_mock_build_chest_open_result_response(out, outCap, result);
        printf("[info][network] mock_chest_open box_seq=%u key_seq=%u result=%u "
               "reason=box-missing-or-unsupported resp=%u "
               "evidence=JianghuOL.CBE:0x01023706+0x010261EC\n",
               boxSeq, keySeq, result, pos);
        return pos;
    }
    chestItemId = boxItem->itemId;
    keyItemId = vm_net_mock_chest_key_for_box(chestItemId);
    if (keyItem == NULL || keyItem->count == 0 ||
        keyItem->itemId != keyItemId)
    {
        result = 3;
        pos = vm_net_mock_build_chest_open_result_response(out, outCap, result);
        printf("[info][network] mock_chest_open box_seq=%u key_seq=%u "
               "box=%u need_key=%u result=%u reason=key-missing-or-mismatch "
               "resp=%u evidence=JianghuOL.CBE:0x01023706+0x010261EC\n",
               boxSeq, keySeq, chestItemId, keyItemId, result, pos);
        return pos;
    }
    if (!vm_net_mock_chest_roll_reward_item(chestItemId, &rewardItemId))
    {
        result = 6;
        pos = vm_net_mock_build_chest_open_result_response(out, outCap, result);
        printf("[warn][network] mock_chest_open box_seq=%u key_seq=%u box=%u "
               "result=%u reason=empty-reward-pool resp=%u\n",
               boxSeq, keySeq, chestItemId, result, pos);
        return pos;
    }

    before = *role;
    if (!vm_net_mock_role_consume_backpack_item(
            role, chestItemId, boxSeq, 1, NULL) ||
        !vm_net_mock_role_consume_backpack_item(
            role, keyItemId, keySeq, 1, NULL))
    {
        *role = before;
        result = 6;
        pos = vm_net_mock_build_chest_open_result_response(out, outCap, result);
        printf("[warn][network] mock_chest_open box_seq=%u key_seq=%u box=%u "
               "result=%u reason=consume-failed resp=%u\n",
               boxSeq, keySeq, chestItemId, result, pos);
        return pos;
    }
    if (!vm_net_mock_role_add_backpack_item_to_role(
            role, rewardItemId, 1, 0, &rewardSeq, "chest-open"))
    {
        *role = before;
        result = 4;
        pos = vm_net_mock_build_chest_open_result_response(out, outCap, result);
        printf("[info][network] mock_chest_open box_seq=%u key_seq=%u box=%u "
               "result=%u reason=backpack-full reward=%u resp=%u "
               "evidence=JianghuOL.CBE:0x010266DE\n",
               boxSeq, keySeq, chestItemId, result, rewardItemId, pos);
        return pos;
    }

    /*
     * JianghuOL.CBE:0x01019228 accumulates wire count into the tip out-param
     * (*out += count) used by 「获得%d个%s」 at 0x01026376.  That field is the
     * granted delta for this open, not the post-merge backpack absolute.
     * Sending rewardItem->count after stack-merge made tips like 获得6个 when
     * the bag already had 5 and only +1 was granted; client also ADDs the wire
     * count, so the on-screen stack inflated until relogin restored the server
     * absolute.
     */
    rewardCount = 1;
    if (!vm_net_mock_build_chest_open_info_blob(itemInfo, sizeof(itemInfo),
                                                 rewardSeq, rewardItemId,
                                                 rewardCount, &itemInfoLen) ||
        itemInfoLen == 0 || itemInfoLen > 0xffffu)
    {
        *role = before;
        (void)vm_net_mock_role_db_save("chest-open-rollback");
        result = 6;
        pos = vm_net_mock_build_chest_open_result_response(out, outCap, result);
        printf("[warn][network] mock_chest_open box_seq=%u key_seq=%u box=%u "
               "result=%u reason=info-build-failed reward=%u resp=%u\n",
               boxSeq, keySeq, chestItemId, result, rewardItemId, pos);
        return pos;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 15, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "total", 1) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "iteminfo", itemInfo,
                                    (u16)itemInfoLen))
    {
        *role = before;
        (void)vm_net_mock_role_db_save("chest-open-rollback");
        result = 6;
        pos = vm_net_mock_build_chest_open_result_response(out, outCap, result);
        printf("[warn][network] mock_chest_open box_seq=%u key_seq=%u box=%u "
               "result=%u reason=success-encode-failed reward=%u resp=%u\n",
               boxSeq, keySeq, chestItemId, result, rewardItemId, pos);
        return pos;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);

    (void)vm_net_mock_role_mark_inventory_dirty("chest-open");
    vm_net_mock_gold_chest_maybe_announce_rare_reward(rewardItemId);

    {
        vm_net_mock_backpack_item_state *rewardItem =
            vm_net_mock_role_find_backpack_item(role, rewardItemId, rewardSeq);
        u32 bagAfter = rewardItem != NULL ? rewardItem->count : rewardCount;

        printf("[info][network] mock_chest_open box=%u/%u key=%u/%u reward=%u "
               "seq=%u granted=%u bag_after=%u result=1 resp=%u "
               "evidence=JianghuOL.CBE:0x01023706+0x010261EC+0x01019228+0x01026376\n",
               chestItemId, boxSeq, keyItemId, keySeq,
               rewardItemId, rewardSeq, rewardCount, bagAfter, pos);
        vm_autotest_note("mock_chest_open box=%u key=%u reward=%u seq=%u granted=%u "
                         "result=1 response=7/15 evidence=JianghuOL.CBE:0x01023706+0x010261EC+0x01019228\n",
                         chestItemId, keyItemId, rewardItemId, rewardSeq,
                         rewardCount);
    }
    return pos;
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

/*
 * JianghuOL.CBE only routes 809/810/811 onto 1/7/30 (0x010236CA).  Custom
 * exp cards such as 845 share category 10 but fall through to generic 1/7/1;
 * HandleItemOperationResponse(0x01033544) owns that wait.  Apply the same
 * timed-effect consume as 7/30, then clear the wait with 7/1+7/7+7/11.
 * Official 7/30 (JianghuOL.CBE:0x0102642A) also writes the local expcard
 * flag on result=1; generic 7/1 does not, so push 7/32 {expcard=1} plus
 * 7/31 {expinfo} for the top-left icon (0x01011D48 / 0x01011AF8).
 */
static u32 vm_net_mock_build_exp_card_use_via_generic_7_1(
    vm_net_mock_role_state *role, u32 itemId, u16 seq, u16 enhanceLevel,
    u8 itemUseType, u8 *out, u32 outCap)
{
    const vm_net_mock_item_effect_catalog_item *catalogItem = NULL;
    vm_net_mock_role_item_effect effect;
    u32 multiplier = 0;
    u32 durationSeconds = 0;
    u32 now = (u32)time(NULL);
    u32 remaining = 0;
    bool success = false;
    u8 itemInfo[VM_NET_MOCK_ITEM_USE_ITEMINFO_SCRATCH];
    u32 itemInfoLen = 0;
    u8 countInfo[32];
    u32 countInfoLen = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;

    if (role == NULL || out == NULL || outCap < pos || itemId == 0 || seq == 0)
        return 0;

    memset(&effect, 0, sizeof(effect));
    catalogItem = vm_net_mock_find_item_effect_catalog_item(itemId);
    multiplier = vm_net_mock_exp_card_multiplier_for_item(itemId);
    effect.kind = VM_NET_MOCK_ROLE_ITEM_EFFECT_EXP_CARD;
    effect.itemId = itemId;
    effect.multiplier = multiplier;
    if (catalogItem != NULL && multiplier != 0 &&
        catalogItem->durationMinutes == 60 && catalogItem->category == 10)
    {
        durationSeconds = (u32)catalogItem->durationMinutes * 60u;
    }
    if (durationSeconds != 0 && now <= 0xffffffffu - durationSeconds)
    {
        const char *failInfo = NULL;
        effect.expiresUnix = now + durationSeconds;
        success = vm_net_mock_role_consume_backpack_item_with_timed_effect(
            role, itemId, seq, &effect, &remaining, "exp-card-use-7-1",
            &failInfo);
        if (!success)
        {
            printf("[warn][network] mock_exp_card_generic_7_1_failed item=%u seq=%u "
                   "mult=%u duration_s=%u evidence=runtime:wt7/1-for-custom-exp-card\n",
                   itemId, seq, multiplier, durationSeconds);
            return vm_net_mock_build_item_use_hint_response(
                out, outCap,
                failInfo ? failInfo
                         : "\xBE\xAD\xD1\xE9\xBF\xA8\xCA\xB9\xD3\xC3\xCA\xA7\xB0\xDC\xA3\xAC\xC7\xEB\xC9\xD4\xBA\xF3\xD4\xD9\xCA\xD4\xA1\xA3");
        }
    }
    if (!success)
    {
        printf("[warn][network] mock_exp_card_generic_7_1_failed item=%u seq=%u "
               "mult=%u duration_s=%u evidence=runtime:wt7/1-for-custom-exp-card\n",
               itemId, seq, multiplier, durationSeconds);
        return vm_net_mock_build_item_use_hint_response(
            out, outCap,
            "\xBE\xAD\xD1\xE9\xBF\xA8\xCA\xB9\xD3\xC3\xCA\xA7\xB0\xDC\xA3\xAC\xC7\xEB\xC9\xD4\xBA\xF3\xD4\xD9\xCA\xD4\xA1\xA3");
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 1, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "type", itemUseType ? itemUseType : 1) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "id", (u16)itemId))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    objectCount = 1;

    if (!vm_net_mock_build_item_use_iteminfo_blob(itemInfo, sizeof(itemInfo), seq,
                                                  itemId, remaining, enhanceLevel,
                                                  &itemInfoLen) ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 7, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "type", 2) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "iteminfo", itemInfo,
                                    (u16)itemInfoLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    ++objectCount;

    if (!vm_net_mock_build_item_use_count_info_blob(countInfo, sizeof(countInfo), seq,
                                                    remaining, &countInfoLen) ||
        !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 11, &objectStart) ||
        !vm_net_mock_put_object_raw(out, outCap, &pos, "info", countInfo,
                                    (u16)countInfoLen))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    ++objectCount;

    /*
     * Mirror 7/30 result=1 writing expcard at JianghuOL.CBE:0x0102643C, then
     * the same 7/31 expinfo login/use path uses for the icon glyph.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 32, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "expcard", 1))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    ++objectCount;

    if (!vm_net_mock_append_exp_card_info_object(out, outCap, &pos))
        return 0;
    if (vm_net_mock_role_active_exp_card_multiplier(role) > 1u)
        ++objectCount;

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    printf("[info][network] mock_exp_card_generic_7_1 item=%u seq=%u mult=%u "
           "remaining=%u response=7/1+7/7+7/11+7/32+7/31 resp=%u "
           "evidence=JianghuOL.CBE:0x0102642A-expcard+0x01011AF8-expinfo\n",
           itemId, seq, multiplier, remaining, pos);
    vm_autotest_note("mock_exp_card_generic_7_1 item=%u seq=%u mult=%u remaining=%u "
                     "response=7/1+7/7+7/11+7/32+7/31 evidence=runtime:wt7/1-custom-exp-card-icon\n",
                     itemId, seq, multiplier, remaining);
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
    u16 enhanceLevel = 0;
    bool consumed = false;
    bool applied = false;
    bool reservoirItem = false;
    bool capacityExpanded = false;
    u8 itemInfo[VM_NET_MOCK_ITEM_USE_ITEMINFO_SCRATCH];
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
        enhanceLevel = item->enhanceLevel;
    }
    else
    {
        itemId = parsed.itemId;
        seq = parsed.seq;
    }

    /* A selected special item must be handled by its own client contract.
     * Returning no generic response here intentionally leaves an unexpected
     * 7/1 variant observable instead of consuming it as a false success.
     *
     * Exception: custom exp cards (845) are never hardcoded onto 7/30 by
     * JianghuOL.CBE, so the client submits ordinary 7/1.  Honor that wait
     * with timed-effect consume + 7/1 inventory mutation. */
    if (vm_net_mock_item_requires_special_use_protocol(itemId))
    {
        if (vm_net_mock_exp_card_multiplier_for_item(itemId) != 0)
        {
            return vm_net_mock_build_exp_card_use_via_generic_7_1(
                role, itemId, seq, enhanceLevel,
                parsed.type ? parsed.type : 1, out, outCap);
        }
        return 0;
    }

    /*
     * Mall warehouse pass (834): category must be 10 (not 14).  Runtime evidence
     * for 801 shows backpack refuses category-14 with 不能直接使用 and never
     * emits 7/1.
     *
     * Respond with 7/1+7/7+7/11 only.  Do NOT append 26/1 on the same WT
     * packet: same-packet kind-26 sticks DF_DataPackage_DoLoading.  Transport
     * sends a second CBMR with lone 26/1 on the same data_request (HAS_FOLLOWUP);
     * scene_sync_poll remains fallback if wire followup is not taken.
     */
    if (itemId == VM_NET_MOCK_WAREHOUSE_PASS_ITEM_ID)
    {
        u32 remaining = 0;
        u8 itemInfo[VM_NET_MOCK_ITEM_USE_ITEMINFO_SCRATCH];
        u32 itemInfoLen = 0;
        u8 countInfo[32];
        u32 countInfoLen = 0;
        u8 objectCount = 0;

        /*
         * Prefer the selected row; if seq drifted (ghost/local mmShop insert),
         * fall back to any live 834 with remaining durability.
         */
        if (item == NULL || item->itemId != VM_NET_MOCK_WAREHOUSE_PASS_ITEM_ID ||
            item->count == 0)
        {
            item = vm_net_mock_role_find_backpack_item(
                role, VM_NET_MOCK_WAREHOUSE_PASS_ITEM_ID, 0);
        }
        if (item == NULL || item->count == 0)
        {
            u8 bagCount = vm_net_mock_role_backpack_count(role);
            printf("[warn][network] mock_warehouse_pass_use_reject role=%u "
                   "req_item=%u req_seq=%u bag_rows=%u evidence=no-live-834-durability\n",
                   role->roleId, parsed.itemId, parsed.seq, bagCount);
            return vm_net_mock_build_item_use_hint_response(
                out, outCap,
                "\xb2\xd6\xbf\xe2\xc6\xbe\xd6\xa4\xc4\xcd\xbe\xc3\xb2\xbb\xd7\xe3\xa3\xac\xc7\xeb\xd6\xd8\xd0\xc2\xb9\xba\xc2\xf2\xa1\xa3"); /* 仓库凭证耐久不足，请重新购买。 */
        }
        itemId = item->itemId;
        seq = item->seq;
        if (!vm_net_mock_role_consume_backpack_item(role, itemId, seq, 1,
                                                    &remaining))
        {
            return vm_net_mock_build_item_use_hint_response(
                out, outCap,
                "\xb2\xd6\xbf\xe2\xc6\xbe\xd6\xa4\xc4\xcd\xbe\xc3\xb2\xbb\xd7\xe3\xa3\xac\xc7\xeb\xd6\xd8\xd0\xc2\xb9\xba\xc2\xf2\xa1\xa3");
        }
        vm_net_mock_role_mark_inventory_dirty("warehouse-pass-use");
        vm_mock_service_session_arm_warehouse_pass_dialog();

        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 1,
                                         &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "type", 1) ||
            !vm_net_mock_put_object_u16(out, outCap, &pos, "id", (u16)itemId))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount = 1;
        if (!vm_net_mock_build_item_use_iteminfo_blob(
                itemInfo, sizeof(itemInfo), seq, itemId, remaining, 0,
                &itemInfoLen) ||
            !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 7,
                                         &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "type", 2) ||
            !vm_net_mock_put_object_raw(out, outCap, &pos, "iteminfo", itemInfo,
                                        (u16)itemInfoLen))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        ++objectCount;
        if (!vm_net_mock_build_item_use_count_info_blob(
                countInfo, sizeof(countInfo), seq, remaining, &countInfoLen) ||
            !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 11,
                                         &objectStart) ||
            !vm_net_mock_put_object_raw(out, outCap, &pos, "info", countInfo,
                                        (u16)countInfoLen))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        ++objectCount;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        printf("[info][network] mock_warehouse_pass_use item=%u seq=%u durability=%u->%u dialog=wire-26/1 resp=%u evidence=item.dsh:834-cat10+7/1-then-second-CBMR-26/1\n",
               itemId, seq, remaining + 1u, remaining, pos);
        vm_autotest_note("mock_warehouse_pass_use item=%u seq=%u durability=%u dialog=wire-26/1 response=7/1+7/7+7/11 evidence=runtime:second-CBMR-vs-same-packet-or-poll\n",
                         itemId, seq, remaining);
        return pos;
    }

    /*
     * Mall equipment-sell pass (839): same use contract as warehouse 834
     * (cat 10 durable; 7/1+7/7+7/11 then second-CBMR lone 26/1).
     * Id 835 is 红玫瑰 — never reuse it for this pass.
     */
    if (itemId == VM_NET_MOCK_EQUIP_SELL_PASS_ITEM_ID)
    {
        u32 remaining = 0;
        u8 itemInfo[VM_NET_MOCK_ITEM_USE_ITEMINFO_SCRATCH];
        u32 itemInfoLen = 0;
        u8 countInfo[32];
        u32 countInfoLen = 0;
        u8 objectCount = 0;

        if (item == NULL || item->itemId != VM_NET_MOCK_EQUIP_SELL_PASS_ITEM_ID ||
            item->count == 0)
        {
            item = vm_net_mock_role_find_backpack_item(
                role, VM_NET_MOCK_EQUIP_SELL_PASS_ITEM_ID, 0);
        }
        if (item == NULL || item->count == 0)
        {
            u8 bagCount = vm_net_mock_role_backpack_count(role);
            printf("[warn][network] mock_equip_sell_pass_use_reject role=%u "
                   "req_item=%u req_seq=%u bag_rows=%u evidence=no-live-839-durability\n",
                   role->roleId, parsed.itemId, parsed.seq, bagCount);
            return vm_net_mock_build_item_use_hint_response(
                out, outCap,
                "\xd7\xb0\xb1\xb8\xb3\xf6\xca\xdb\xc6\xbe\xd6\xa4\xc4\xcd\xbe\xc3\xb2\xbb\xd7\xe3\xa3\xac\xc7\xeb\xd6\xd8\xd0\xc2\xb9\xba\xc2\xf2\xa1\xa3"); /* 装备出售凭证耐久不足，请重新购买。 */
        }
        itemId = item->itemId;
        seq = item->seq;
        if (!vm_net_mock_role_consume_backpack_item(role, itemId, seq, 1,
                                                    &remaining))
        {
            return vm_net_mock_build_item_use_hint_response(
                out, outCap,
                "\xd7\xb0\xb1\xb8\xb3\xf6\xca\xdb\xc6\xbe\xd6\xa4\xc4\xcd\xbe\xc3\xb2\xbb\xd7\xe3\xa3\xac\xc7\xeb\xd6\xd8\xd0\xc2\xb9\xba\xc2\xf2\xa1\xa3");
        }
        vm_net_mock_role_mark_inventory_dirty("equip-sell-pass-use");
        vm_mock_service_session_arm_equip_sell_pass_dialog();

        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 1,
                                         &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "type", 1) ||
            !vm_net_mock_put_object_u16(out, outCap, &pos, "id", (u16)itemId))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount = 1;
        if (!vm_net_mock_build_item_use_iteminfo_blob(
                itemInfo, sizeof(itemInfo), seq, itemId, remaining, 0,
                &itemInfoLen) ||
            !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 7,
                                         &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "type", 2) ||
            !vm_net_mock_put_object_raw(out, outCap, &pos, "iteminfo", itemInfo,
                                        (u16)itemInfoLen))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        ++objectCount;
        if (!vm_net_mock_build_item_use_count_info_blob(
                countInfo, sizeof(countInfo), seq, remaining, &countInfoLen) ||
            !vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 11,
                                         &objectStart) ||
            !vm_net_mock_put_object_raw(out, outCap, &pos, "info", countInfo,
                                        (u16)countInfoLen))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        ++objectCount;
        vm_net_mock_finish_wt_packet(out, pos, objectCount);
        printf("[info][network] mock_equip_sell_pass_use item=%u seq=%u durability=%u->%u dialog=wire-26/1 resp=%u evidence=item.dsh:839-cat10+7/1-then-second-CBMR-26/1\n",
               itemId, seq, remaining + 1u, remaining, pos);
        vm_autotest_note("mock_equip_sell_pass_use item=%u seq=%u durability=%u dialog=wire-26/1 response=7/1+7/7+7/11 evidence=runtime:second-CBMR-vs-same-packet-or-poll\n",
                         itemId, seq, remaining);
        return pos;
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
        vm_net_mock_role_mark_inventory_dirty("item-use");

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
                                                      enhanceLevel,
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
    vm_net_mock_role_state before;
    vm_net_mock_backpack_item_state *item = NULL;
    u32 itemId = 0;
    u16 seq = 0;
    u32 discardCount = 0;
    u32 remaining = 0;
    u32 refundMoney = 0;
    bool consumed = false;
    bool haveBefore = false;
    u8 result = 2;
    u8 countInfo[32];
    u32 countInfoLen = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    char refreshLabel[64];
    bool appended17 = false;
    bool appended42 = false;
    bool appended11 = false;
    bool appendedMoney = false;

    if (out == NULL || outCap < pos)
        return 0;
    /*
     * Recognized 7/4 must never return 0 — HandleItemOperationResponse
     * (JianghuOL.CBE:0x01033544) only clears the item-op waiting flag when
     * subtype 4 arrives.  Empty CBMR bodies look like a permanent hang and
     * force re-enter after continuous discards.
     */
    if (!vm_net_mock_parse_item_discard_request(request, requestLen, &parsed))
    {
        u8 wtKind = 0;
        u8 wtSubtype = 0;
        bool looksLikeDiscard =
            vm_net_mock_request_contains_object(request, requestLen, 1, 7, 4) ||
            (vm_net_mock_get_wt_header_kind_subtype(request, requestLen,
                                                     &wtKind, &wtSubtype) &&
             wtKind == 7 && wtSubtype == 4);

        /*
         * Header/body failed the narrow selector parse.  Any recognizable
         * 7/4 must still emit result=2 so the wait flag clears instead of
         * ignored-unhandled response=0.
         */
        if (!looksLikeDiscard)
            return 0;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 4, &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 2))
        {
            printf("[error][network] mock_item_discard_encode_failed phase=7/4-unparsed "
                   "len=%u evidence=recognized-7/4-must-not-return-0\n",
                   requestLen);
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        vm_net_mock_finish_wt_packet(out, pos, 1);
        printf("[warn][network] mock_item_discard_unparsed len=%u result=2 "
               "refresh=7/4-fail evidence=clear-wait-on-malformed-7/4\n",
               requestLen);
        return pos;
    }

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
            const vm_net_mock_equipment_catalog_item *equipment =
                vm_net_mock_find_equipment_catalog_item(item->itemId);
            const vm_net_mock_shop_catalog_item *catalog = NULL;

            itemId = item->itemId;
            seq = item->seq;
            discardCount = parsed.count ? parsed.count : item->count;
            if (discardCount == 0)
                discardCount = item->count;
            /*
             * Equipment rows must never discard the whole same-name pile when
             * the client omits count: one confirm removes one instance.
             */
            if (parsed.count == 0 && equipment != NULL)
                discardCount = 1;
            before = *role;
            haveBefore = true;
            consumed = vm_net_mock_role_consume_backpack_item(role, itemId, seq,
                                                              discardCount, &remaining);
            if (consumed)
            {
                result = 1;
                /*
                 * Equipment discard refunds floor(equip.dsh 价值 / 10) copper
                 * per removed instance.  Ordinary item.dsh rows get no refund.
                 */
                if (equipment != NULL)
                {
                    catalog = vm_net_mock_find_shop_catalog_item(itemId);
                    if (catalog != NULL && catalog->price >= 10u)
                    {
                        refundMoney = vm_net_mock_mul_capped_u32(
                            catalog->price / 10u, discardCount);
                        if (refundMoney != 0)
                        {
                            role->money = vm_net_mock_add_capped_u32(
                                role->money, refundMoney);
                        }
                    }
                }
            }
        }
        else
        {
            u8 bagCount = vm_net_mock_role_backpack_count(role);
            char seqDump[160];
            u32 dumpPos = 0;

            itemId = parsed.itemId;
            seq = parsed.seq;
            /*
             * Client often keeps ghost bag rows after warehouse deposit (26/1
             * cannot carry 17/1).  7/4 result=2 surfaces as「网络异常」.  The
             * authoritative end state is already "row absent", so clear the
             * wait flag with result=1 and force 17/1 list reconciliation.
             */
            result = 1;
            seqDump[0] = 0;
            for (u32 i = 0; i < bagCount && dumpPos + 12 < sizeof(seqDump); ++i)
            {
                int wrote = snprintf(seqDump + dumpPos, sizeof(seqDump) - dumpPos,
                                     "%s%u", i ? "," : "",
                                     role->backpackItems[i].seq);
                if (wrote > 0)
                    dumpPos += (u32)wrote;
            }
            printf("[warn][network] mock_item_discard_stale_resync "
                   "req_item=%u req_seq=%u bag_rows=%u bag_seqs=[%s] "
                   "evidence=result1+17/1-clear-ghost-avoid-network-error\n",
                   parsed.itemId, parsed.seq, bagCount, seqDump);
        }
    }

    /*
     * JianghuOL.CBE:0x1033544 handles 7/4 as the item-operation completion
     * branch and clears the waiting flag.  The backpack UI callback is the
     * proven mmGame:0x418C path, so a successful discard also sends a full
     * 17/1 list rebuild plus 7/42 book filler.
     *
     * Copper refund must not use 1/1/14 here: mmShop owns that actor-state
     * branch, and the backpack discard response is consumed by kind-7 /
     * 17/1 parsers only.  Append the proven group/type-1 money object
     * (1/10/26) so the backpack copper label tracks role->money immediately.
     * Do not reuse 7/26: that subtype opens the task hall (0x01010C34).
     *
     * Follow-up objects are best-effort: failing 17/1/7/42/7/11/10/26 must
     * still deliver 7/4 so continuous discard cannot stick the waiting flag.
     */
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 4, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", result))
    {
        if (haveBefore && role != NULL)
            *role = before;
        consumed = false;
        result = 2;
        refundMoney = 0;
        pos = 5;
        objectCount = 0;
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 4, &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 2))
        {
            printf("[error][network] mock_item_discard_encode_failed phase=7/4 "
                   "item=%u seq=%u evidence=recognized-7/4-must-not-return-0\n",
                   itemId, seq);
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount = 1;
    }
    else
    {
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        objectCount += 1;
    }

    if (consumed || result == 1)
    {
        if (vm_net_mock_append_backpack_items_object(out, outCap, &pos))
        {
            objectCount += 1;
            appended17 = true;
        }
        else
        {
            printf("[warn][network] mock_item_discard_encode_failed phase=17/1 "
                   "item=%u seq=%u role=%u evidence=keep-7/4-clear-wait-flag\n",
                   itemId, seq, role ? role->roleId : 0);
        }
        if (vm_net_mock_append_books42_object(out, outCap, &pos))
        {
            objectCount += 1;
            appended42 = true;
        }
        else
        {
            printf("[warn][network] mock_item_discard_encode_failed phase=7/42 "
                   "item=%u seq=%u evidence=keep-7/4-clear-wait-flag\n",
                   itemId, seq);
        }
        if (consumed && seq != 0 &&
            vm_net_mock_build_item_use_count_info_blob(countInfo, sizeof(countInfo),
                                                       seq, remaining, &countInfoLen) &&
            vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 11, &objectStart) &&
            vm_net_mock_put_object_raw(out, outCap, &pos, "info", countInfo,
                                       (u16)countInfoLen))
        {
            vm_net_mock_finish_wt_object(out, objectStart, pos);
            objectCount += 1;
            appended11 = true;
        }
        else if (consumed && seq != 0)
        {
            printf("[warn][network] mock_item_discard_encode_failed phase=7/11 "
                   "item=%u seq=%u evidence=keep-7/4-clear-wait-flag\n",
                   itemId, seq);
        }
        if (consumed && refundMoney != 0)
        {
            u32 moneyPos = pos;
            u8 moneyObjects = objectCount;

            /* Same contract as vm_net_mock_append_type1_object / group-type1. */
            if (vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 0x0a, 0x1a,
                                             &objectStart) &&
                vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) &&
                vm_net_mock_put_object_u8(out, outCap, &pos, "type", 1) &&
                vm_net_mock_put_object_u8(out, outCap, &pos, "npcnum", 0) &&
                vm_net_mock_put_object_string(out, outCap, &pos, "name",
                                              "\xce\xde") && /* 无 */
                vm_net_mock_put_object_u32(out, outCap, &pos, "money",
                                            role ? role->money : 0))
            {
                vm_net_mock_finish_wt_object(out, objectStart, pos);
                objectCount += 1;
                appendedMoney = true;
            }
            else
            {
                pos = moneyPos;
                objectCount = moneyObjects;
                printf("[warn][network] mock_item_discard_encode_failed phase=10/26 "
                       "item=%u seq=%u refund=%u evidence=keep-7/4-clear-wait-flag\n",
                       itemId, seq, refundMoney);
            }
        }

        /*
         * Do not role_db_save here.  Full backpack DELETE+INSERT under the
         * protocol lock routinely exceeds remote poll/data budgets after a
         * burst of equipment discards; the client then never sees 7/4 and
         * sticks on the item-op wait flag.  Memory + CBMR are authoritative
         * for the session; MySQL flushes after send / on disconnect.
         */
        if (consumed)
        {
            vm_net_mock_role_mark_inventory_dirty("item-discard");
        }
    }

    vm_net_mock_finish_wt_packet(out, pos, objectCount);
    if (result != 1)
        snprintf(refreshLabel, sizeof(refreshLabel), "7/4-fail");
    else if (!consumed)
        snprintf(refreshLabel, sizeof(refreshLabel), "7/4-stale-resync%s%s",
                 appended17 ? "+17/1" : "",
                 appended42 ? "+7/42" : "");
    else
    {
        snprintf(refreshLabel, sizeof(refreshLabel), "7/4%s%s%s%s",
                 appended17 ? "+17/1" : "",
                 appended42 ? "+7/42" : "",
                 appended11 ? "+7/11" : "",
                 appendedMoney ? "+10/26" : "");
    }
    printf("[info][network] mock_item_discard item=%u seq=%u count=%u remaining=%u refund=%u money=%u result=%u refresh=%s resp=%u\n",
           itemId, seq, discardCount, remaining, refundMoney,
           role ? role->money : 0, result, refreshLabel, pos);
    vm_autotest_note("mock_item_discard item=%u seq=%u count=%u remaining=%u refund=%u result=%u response=%s evidence=runtime:wt7/4 JianghuOL.CBE:0x1033544+0x010126C6 mmGame:0x418C\n",
                     itemId, seq, discardCount, remaining, refundMoney, result,
                     refreshLabel);
    return pos;
}

static bool vm_net_mock_append_backpack_items_object(u8 *out, u32 outCap, u32 *pos)
{
    u32 objectStart = 0;
    u8 itemInfo[VM_NET_MOCK_BACKPACK_ITEMINFO_SCRATCH];
    u32 itemInfoLen = 0;
    u32 rowCount = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u16 capacity = vm_net_mock_backpack_client_capacity(
        role ? role->backpackCapacity : VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY);

    if (out == NULL || pos == NULL)
        return false;
    memset(itemInfo, 0, sizeof(itemInfo));
    if (!vm_net_mock_build_backpack_iteminfo_blob(itemInfo, sizeof(itemInfo), role,
                                                 &itemInfoLen, &rowCount))
    {
        printf("[error][network] mock_backpack_items_encode_failed role=%u "
               "stored_rows=%u scratch=%u evidence=iteminfo-overflow\n",
               role ? role->roleId : 0,
               vm_net_mock_role_backpack_count(role),
               (u32)sizeof(itemInfo));
        return false;
    }
    if (itemInfoLen == 0 || itemInfoLen > 0xffff)
        return false;

    if (!vm_net_mock_begin_wt_object(out, outCap, pos, 1, 17, 1, &objectStart))
        return false;
    if (!vm_net_mock_put_object_u16(out, outCap, pos, "maxnum", capacity))
        return false;
    if (!vm_net_mock_put_object_raw(out, outCap, pos, "iteminfo", itemInfo, (u16)itemInfoLen))
        return false;
    vm_net_mock_finish_wt_object(out, objectStart, *pos);

    printf("[info][network] mock_backpack_items role=%u capacity=%u rows=%u stored_rows=%u iteminfo_len=%u\n",
           role ? role->roleId : 0,
           capacity,
           rowCount,
           vm_net_mock_role_backpack_count(role),
           itemInfoLen);
    vm_autotest_note("mock_backpack_items role=%u capacity=%u rows=%u stored_rows=%u iteminfo_len=%u evidence=mmGame:0x418C+mmShop:sub_9DE\n",
                     role ? role->roleId : 0,
                     capacity,
                     rowCount,
                     vm_net_mock_role_backpack_count(role),
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
    u8 itemInfo[VM_NET_MOCK_BACKPACK_ITEMINFO_SCRATCH];
    u32 itemInfoLen = 0;
    u32 gridCount = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();

    if (out == NULL || pos == NULL)
        return false;
    memset(itemInfo, 0, sizeof(itemInfo));
    if (!vm_net_mock_build_backpack_grid_iteminfo_blob(itemInfo, sizeof(itemInfo), role,
                                                      &itemInfoLen, &gridCount))
    {
        printf("[error][network] mock_backpack_grid_encode_failed role=%u "
               "stored_rows=%u scratch=%u evidence=iteminfo-overflow-30/21 "
               "group-type1-would-drop\n",
               role ? role->roleId : 0,
               vm_net_mock_role_backpack_count(role),
               (u32)sizeof(itemInfo));
        return false;
    }
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

    printf("[info][network] mock_backpack_grid role=%u kind=30 subtype=21 gridnum=%u stored_rows=%u iteminfo_len=%u\n",
           role ? role->roleId : 0,
           gridCount,
           vm_net_mock_role_backpack_count(role),
           itemInfoLen);
    vm_autotest_note("mock_backpack_grid role=%u kind=30 subtype=21 gridnum=%u stored_rows=%u iteminfo_len=%u evidence=JianghuOL:0x1039952+mmShop:sub_9DE\n",
                     role ? role->roleId : 0,
                     gridCount,
                     vm_net_mock_role_backpack_count(role),
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
    u8 itemInfo[VM_NET_MOCK_EQUIP_LOGIN_ITEMINFO_SCRATCH];
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
        printf("[error][network] mock_equipment_login_encode_failed role=%u "
               "scratch=%u evidence=iteminfo-overflow-7/7-type2 "
               "group-type1-would-drop\n",
               role->roleId,
               (u32)sizeof(itemInfo));
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
        printf("[error][network] mock_equipment_login_put_failed role=%u "
               "rows=%u iteminfo_len=%u out_pos=%u out_cap=%u "
               "evidence=7/7-type2-object-encode\n",
               role->roleId, rowCount, itemInfoLen,
               pos ? *pos : 0, outCap);
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

        if (vm_net_mock_role_backpack_client_grid_count(role) != 0)
        {
            if (!vm_net_mock_append_backpack_grid_object(out, outCap, pos))
            {
                printf("[error][network] mock_group_type1_backpack_step_failed "
                       "role=%u step=30/21 evidence=grid-encode\n",
                       role->roleId);
                return false;
            }
            *objectCount = (u8)(*objectCount + 1);
            if (!vm_net_mock_append_backpack_reservoir_counts_object(
                    out, outCap, pos, &appendedReservoirCounts))
            {
                printf("[error][network] mock_group_type1_backpack_step_failed "
                       "role=%u step=7/11 evidence=reservoir-encode\n",
                       role->roleId);
                return false;
            }
            if (appendedReservoirCounts)
                *objectCount = (u8)(*objectCount + 1);
        }
        if (!vm_net_mock_append_equipment_login_object(
                out, outCap, pos, &equipmentRows))
        {
            printf("[error][network] mock_group_type1_backpack_step_failed "
                   "role=%u step=7/7-type2 evidence=equipment-login-encode\n",
                   role->roleId);
            return false;
        }
        *objectCount = (u8)(*objectCount + 1);
        /*
         * Do NOT append 1/1/14 with the full login actorinfo blob here.
         * parse_actorinfo_response(a2!=0) skips several u32 reads (base HP/MP,
         * EXTRA132, trailing strings) without consuming them — a full blob
         * desyncs the stream and corrupts later halfwords.  Battle/map vitals
         * refresh uses the same 1/1/14 path with the live-update contract;
         * login property authority is subtype-6 actorinfo + wear apply.
         */
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
static u32 g_vm_net_mock_battle_rewarded_serial = 0;
static u32 g_vm_net_mock_battle_rewarded_exp = 0;
/* Set to the battle serial when EXP/drops were suppressed by the settlement
 * interval gate so companion gold payout can stay zero for that battle. */
static u32 g_vm_net_mock_battle_reward_rate_suppressed_serial = 0;
static vm_net_mock_battle_drop_result
    g_vm_net_mock_battle_rewarded_drops[VM_NET_MOCK_BATTLE_DROP_RESULT_MAX];
static u8 g_vm_net_mock_battle_rewarded_drop_result_count = 0;
static u32 g_vm_net_mock_battle_enemy_id_current = VM_NET_MOCK_BATTLE_POISON_SLIME_ID;
static u32 g_vm_net_mock_battle_role_id_current = VM_NET_MOCK_ROLE_DEFAULT_ID;
static u32 g_vm_net_mock_battle_reward_rng = 0;
static u32 g_vm_net_mock_battle_settlement_sent_serial = 0;
static u32 g_vm_net_mock_battle_drop_refresh_sent_serial = 0;
static u32 g_vm_net_mock_battle_recovered_serial = 0;
static char g_vm_net_mock_scene_moveinfo_npc_pending_scene[64];
static bool g_vm_net_mock_scene_moveinfo_npc_pending = false;
/*
 * Full-bootstrap arms the one-shot catalog for the later post-enter 25/5
 * (immediate=0).  Scene-sync poll must not consume it first — poll 27/11 does
 * not create nodes on the fresh shell (docs/re/2026-07-24-scene-npc-return).
 */
static bool g_vm_net_mock_scene_moveinfo_npc_wait_post_enter = false;
/*
 * Teleport-stone current-scene completion (download=0) must close loading with
 * 30/2(no-posinfo) but leave the nonempty 27/11 for the following WT6/1 — the
 * first scene-runtime follow-up where ParseMinfoAndSpawnNPCs can keep nodes.
 * Tongquetai was the first proven case; 临安府 map-stone hits the same race.
 */
static bool g_vm_net_mock_scene_moveinfo_npc_wait_wt6 = false;
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
        g_vm_net_mock_scene_moveinfo_npc_wait_post_enter = false;
        g_vm_net_mock_scene_moveinfo_npc_wait_wt6 = false;
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
    g_vm_net_mock_scene_moveinfo_npc_wait_post_enter = false;
    g_vm_net_mock_scene_moveinfo_npc_wait_wt6 = false;
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

    /* Scene identity is byte-for-byte, except that open_server_scene_resource
     * itself may add the optional .sce suffix to the same key.  In particular,
     * c00蓬莱仙岛_02.sce, 00蓬莱仙岛_02.sce and 00_蓬莱仙岛02.sce are not aliases. */
    if (!vm_net_mock_open_server_scene_resource(scene, &fp, NULL, 0))
        return false;
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
    if (!(parsed.subtype == 1
              ? vm_net_mock_get_object_number_field(object.payload,
                                                     object.payloadLen,
                                                     seqField, &seqValue)
              : vm_net_mock_get_object_tagged_number_entry(
                    object.payload, object.payloadLen, seqField, &seqValue)) ||
        seqValue == 0 || seqValue > 0xffffu)
    {
        return false;
    }
    parsed.equipSeq = (u16)seqValue;
    if (parsed.subtype != 1)
    {
        /* The client flushes its tagged material stream directly into the
         * occultinfo entry.  Request fields therefore have no nested blob
         * length, unlike server response raw fields. */
        if (!vm_net_mock_get_object_entry_bytes(
                object.payload, object.payloadLen, "occultinfo", &rawValue,
                &rawValueLen))
        {
            return false;
        }
        parsed.occultInfo = rawValue;
        parsed.occultInfoLen = rawValueLen;
        if (parsed.occultInfoLen == 0 || parsed.occultInfoLen > 45 ||
            parsed.occultInfoLen % 9 != 0)
        {
            return false;
        }
        parsed.materialRows = (u8)(parsed.occultInfoLen / 9);
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

/* A durable scene reference comes from a server/client scene-transition
 * contract.  It may name a resource still pending WT18/7, so persistence must
 * not reinterpret local lookup failure as permission to replace its key. */
static bool vm_net_mock_scene_name_is_persistable(const char *scene)
{
    return vm_net_mock_scene_name_is_download_key(scene);
}

static bool vm_net_mock_read_runtime_scene_name(char *out, size_t outCap)
{
#ifdef CBE_SERVER_ONLY
    /* The authoritative service never owns an emulated scene object.  Session
     * and role state are the only scene authority on this side of CBMS. */
    if (out != NULL && outCap != 0)
        out[0] = 0;
    return false;
#else
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
#endif
}

static const char *vm_net_mock_normalize_scene_name_for_enter(const char *scene)
{
    static char normalized[64];
    if (!vm_net_mock_scene_name_is_persistable(scene))
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

