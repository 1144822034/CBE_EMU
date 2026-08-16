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

/* A successful inventory mutation is acknowledged by its owning operation
 * (for NPC services: 26/1).  The next native backpack query must then use
 * the role-list parser, even if a former mall request still left its
 * one-shot 17/1 context armed.  Do not reset the grid seed here: the client
 * item manager already exists and 17/1 is the parser that replaces the
 * visible backpack list after an in-place mutation. */
static void vm_net_mock_backpack_queue_authoritative_role_list(
    const char *reason)
{
    g_netMockShop17ListPending = 0;
    g_netMockBackpackPreferRoleListAfterShopBuy = 1;
    printf("[info][network] mock_backpack_role_list_pending source=%s shop_pending=0 role_list=1\n",
           reason != NULL ? reason : "mutation");
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
    /* equip.dsh \"品质\" (column 6).  This is catalog metadata, not the
     * per-instance enhancement level stored with a character's equipment. */
    u8 quality;
    u8 category;
    u8 enabled;
    /* Store placement is independent from item.dsh/equip.dsh category.  The
     * latter drives client item semantics and equipment slots, so an admin
     * must never have to rewrite it merely to change a mall page. */
    u8 shopSection;
} vm_net_mock_shop_catalog_item;

/* item.dsh defines the three chest and key identities, but deliberately does
 * not contain reward rows or rates. Those are online-service authority and
 * are therefore stored in MySQL rather than inferred from client resources. */
enum
{
    VM_NET_MOCK_CHEST_KIND_COUNT = 3,
    VM_NET_MOCK_CHEST_REWARD_MAX = 120,
    VM_NET_MOCK_CHEST_REWARD_COUNT_MAX = 99,
    VM_NET_MOCK_CHEST_REWARD_WEIGHT_MAX = 1000000
};

typedef struct
{
    u32 itemId;
    u32 count;
    u32 weight;
    /* The flag is configured per reward row rather than per chest.  A chest
     * can therefore keep routine drops quiet while announcing only a rare
     * configured result. */
    u8 worldBroadcast;
} vm_net_mock_chest_reward;

typedef struct
{
    u32 chestItemId;
    u32 keyItemId;
    u8 rewardCount;
    vm_net_mock_chest_reward rewards[VM_NET_MOCK_CHEST_REWARD_MAX];
} vm_net_mock_chest_admin_row;

typedef struct
{
    u32 chestItemId;
    u32 keyItemId;
    const char *name;
    const char *keyName;
} vm_net_mock_chest_kind;

static const vm_net_mock_chest_kind g_vm_net_mock_chest_kinds[
    VM_NET_MOCK_CHEST_KIND_COUNT] = {
        {522u, 813u, "青铜宝箱", "青铜钥匙"},
        {523u, 814u, "白银宝箱", "白银钥匙"},
        {524u, 815u, "黄金宝箱", "黄金钥匙"}};

enum
{
    VM_NET_MOCK_SHOP_SECTION_AUTO = 0,
    VM_NET_MOCK_SHOP_SECTION_SECRET = 1,
    VM_NET_MOCK_SHOP_SECTION_NORMAL = 2
};

static bool vm_net_mock_shop_item_is_secret_treasure(
    const vm_net_mock_shop_catalog_item *item)
{
    if (item == NULL || item->isEquip)
        return false;
    if (item->shopSection == VM_NET_MOCK_SHOP_SECTION_SECRET)
        return true;
    if (item->shopSection == VM_NET_MOCK_SHOP_SECTION_NORMAL)
        return false;
    return item->category == 14;
}

typedef struct
{
    u32 itemId;
    u8 slot;
    u8 levelRequired;
    /* `装备品质`: quality 0 is the common-equipment reference used by the
     * default monster curve.  Keep this catalog field rather than deriving
     * quality from an item-id range: the DSH data is authoritative. */
    u8 quality;
    /* `装备类型`: distinguishes sword/dagger/staff for the shared weapon
     * slot and is therefore required to construct a job-appropriate outfit. */
    u8 category;
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
    /* skill.dsh `目标指向`: 0=self, 1=one friendly, 2=friendly group,
     * 3=one enemy, 4=enemy group. */
    u8 targetDirection;
    u8 durationRounds;
    /* skill.dsh column 25 (`效果`) distinguishes the non-HP target effects
     * (currently silence=1, dispel=2 and revive=3) from an ordinary spell. */
    u8 effectKind;
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
static vm_net_mock_chest_admin_row
    g_vm_net_mock_chest_rows[VM_NET_MOCK_CHEST_KIND_COUNT];
static bool g_vm_net_mock_chest_db_loaded = false;
static bool g_vm_net_mock_chest_db_valid = false;
static u32 g_vm_net_mock_chest_reward_rng = 0;
static u32 g_vm_net_mock_chest_reward_rng_serial = 0;
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
static const vm_net_mock_equipment_catalog_item *
vm_net_mock_find_equipment_catalog_item(u32 itemId);

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
                                               u32 effectKind,
                                               int32_t strengthChange,
                                               int32_t agilityChange,
                                               int32_t wisdomChange,
                                               int32_t attackChange,
                                               int32_t defenseChange,
                                               int32_t critChange,
                                               int32_t hitChange,
                                               int32_t dodgeChange,
                                               int32_t resistChange,
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
    skill->effectKind = (u8)(effectKind > 255 ? 255 : effectKind);
    skill->strengthChange = strengthChange;
    skill->agilityChange = agilityChange;
    skill->wisdomChange = wisdomChange;
    skill->attackChange = attackChange;
    skill->defenseChange = defenseChange;
    skill->critChange = critChange;
    skill->hitChange = hitChange;
    skill->dodgeChange = dodgeChange;
    skill->resistChange = resistChange;
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
        u32 effectKind = 0;
        int32_t strengthChange = 0;
        int32_t agilityChange = 0;
        int32_t wisdomChange = 0;
        int32_t attackChange = 0;
        int32_t defenseChange = 0;
        int32_t critChange = 0;
        int32_t hitChange = 0;
        int32_t dodgeChange = 0;
        int32_t resistChange = 0;
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
                                               effectKind,
                                               strengthChange,
                                               agilityChange,
                                               wisdomChange,
                                               attackChange,
                                               defenseChange,
                                               critChange,
                                               hitChange,
                                               dodgeChange,
                                               resistChange,
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
                                                0,
                                                0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                (const u8 *)"\xcd\xf2\xbd\xa3\xd6\xef\xcf\xc9\x31",
                                                9);
        (void)vm_net_mock_add_skill_catalog_item(101, 1, 1, 1, 50, 20,
                                                -75, 0, 50, 0, 3, 0,
                                                0,
                                                0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                (const u8 *)"\xb7\xe7\xce\xe8\xc8\xd0\xd0\xd0\x31",
                                                9);
        (void)vm_net_mock_add_skill_catalog_item(201, 2, 1, 7, 50, 5,
                                                -30, 0, 0, 110, 3, 0,
                                                0,
                                                0, 0, 0, 0, 0, 0, 0, 0, 0,
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

/* `item.dsh` intentionally leaves item 921's static description empty.  The
 * client asks for its per-instance text through 7/38 and 7/40, so the record
 * belongs to the same account/role/sequence identity as the backpack row. */
typedef struct
{
    bool found;
    bool invalid;
    char title[49];
    char description[201];
    char bookInfo[201];
    u32 level;
    u32 experience;
} vm_net_mock_training_book_record;

static bool g_vm_net_mock_training_book_schema_checked = false;
static bool g_vm_net_mock_training_book_schema_valid = false;

static const char g_vm_net_mock_training_book_default_title[] =
    "\xD0\xDE\xC1\xB6\xCC\xEC\xCA\xE9"; /* 修炼天书 */
static const char g_vm_net_mock_training_book_default_description[] =
    "\xD0\xDE\xC1\xB6\xCC\xEC\xCA\xE9\n"
    "\xB4\xCB\xCA\xE9\xBC\xC7\xC2\xBC\xC1\xCB\xC7\xB0\xB1\xB2\xB5\xC4\xD0\xDE\xD0\xD0\xD0\xC4\xB5\xC3\xA1\xA3\n"
    "\xCC\xEC\xCA\xE9\xB5\xC8\xBC\xB6\xA3\xBA" "10\n"
    "\xCB\xF9\xBA\xAC\xBE\xAD\xD1\xE9\xA3\xBA" "1689";

/* item.dsh gives no numeric book payload.  The default instance is therefore
 * an explicit server balance seed: level 10 and the current level-10 entry
 * threshold (1689 EXP on the shipped curve).  Per-instance DB values remain
 * authoritative and may be higher; this seed only prevents a newly granted
 * 921 from being an unuseable level-1/zero-EXP placeholder. */
enum
{
    VM_NET_MOCK_TRAINING_BOOK_DEFAULT_LEVEL = 10,
    VM_NET_MOCK_TRAINING_BOOK_DEFAULT_EXPERIENCE = 1689
};

static bool vm_net_mock_training_book_schema_prepare(void)
{
    if (g_vm_net_mock_training_book_schema_checked)
        return g_vm_net_mock_training_book_schema_valid;
    g_vm_net_mock_training_book_schema_checked = true;
    g_vm_net_mock_training_book_schema_valid = vm_mysql_exec(
        "CREATE TABLE IF NOT EXISTS account_role_training_books ("
        "account_id VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
        "role_id INT UNSIGNED NOT NULL,item_seq SMALLINT UNSIGNED NOT NULL,"
        "title VARBINARY(48) NOT NULL,book_description VARBINARY(200) NOT NULL,"
        "book_info VARBINARY(200) NOT NULL,book_level SMALLINT UNSIGNED NOT NULL DEFAULT 1,"
        "book_experience INT UNSIGNED NOT NULL DEFAULT 0,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "PRIMARY KEY(account_id,role_id,item_seq),"
        "CONSTRAINT fk_account_role_training_books_role FOREIGN KEY(account_id,role_id) "
        "REFERENCES account_roles(account_id,role_id) ON DELETE CASCADE"
        ") ENGINE=InnoDB");
    if (!g_vm_net_mock_training_book_schema_valid)
    {
        printf("[error][network] mock_training_book_schema error=%s\n",
               vm_mysql_last_error());
    }
    return g_vm_net_mock_training_book_schema_valid;
}

static bool vm_net_mock_training_book_role_has_instances(const vm_net_mock_role_state *role)
{
    u8 itemCount = vm_net_mock_role_backpack_count(role);

    for (u32 index = 0; index < itemCount; ++index)
    {
        const vm_net_mock_backpack_item_state *item = &role->backpackItems[index];
        if (item->itemId == 921 && item->seq != 0 && item->count != 0)
            return true;
    }
    return false;
}

/* Called inside the role/backpack transaction, after account_role_backpack has
 * been written.  Existing rows are retained so future book progress is never
 * reset by an unrelated position, combat, or equipment save; only orphan rows
 * are removed and missing rows receive the durable default instance data. */
static bool vm_net_mock_training_book_sync_role_records(const vm_net_mock_role_db_file *database,
                                                        const char *accountHex,
                                                        bool fullSnapshot,
                                                        u32 scopedRoleId)
{
    char query[1024];
    char titleHex[sizeof(g_vm_net_mock_training_book_default_title) * 2 + 1];
    char descriptionHex[sizeof(g_vm_net_mock_training_book_default_description) * 2 + 1];
    size_t titleLen = sizeof(g_vm_net_mock_training_book_default_title) - 1;
    size_t descriptionLen = sizeof(g_vm_net_mock_training_book_default_description) - 1;

    if (database == NULL || accountHex == NULL || accountHex[0] == 0 ||
        !vm_net_mock_training_book_schema_prepare() ||
        vm_mysql_hex_encode(g_vm_net_mock_training_book_default_title, titleLen,
                            titleHex, sizeof(titleHex)) == 0 ||
        vm_mysql_hex_encode(g_vm_net_mock_training_book_default_description, descriptionLen,
                            descriptionHex, sizeof(descriptionHex)) == 0)
    {
        return false;
    }

    /* Run orphan cleanup even when this write removed the last book in the
     * scope.  Deciding from the post-mutation inventory alone used to leave
     * account_role_training_books rows behind after an instance disappeared. */
    if (fullSnapshot)
    {
        snprintf(query, sizeof(query),
                 "DELETE books FROM account_role_training_books AS books "
                 "LEFT JOIN account_role_backpack AS bag ON "
                 "bag.account_id=books.account_id AND bag.role_id=books.role_id "
                 "AND bag.item_seq=books.item_seq AND bag.item_id=921 "
                 "WHERE books.account_id=CAST(X'%s' AS CHAR) AND bag.item_seq IS NULL",
                 accountHex);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "DELETE books FROM account_role_training_books AS books "
                 "LEFT JOIN account_role_backpack AS bag ON "
                 "bag.account_id=books.account_id AND bag.role_id=books.role_id "
                 "AND bag.item_seq=books.item_seq AND bag.item_id=921 "
                 "WHERE books.account_id=CAST(X'%s' AS CHAR) AND books.role_id=%u "
                 "AND bag.item_seq IS NULL",
                 accountHex, scopedRoleId);
    }
    if (!vm_mysql_exec(query))
        return false;

    /* Version one of this companion table was created before a transfer rule
     * existed and seeded every 921 as level 1 / zero EXP.  An early build of
     * this transfer implementation also paired level 10 with the level-9
     * threshold 1303.  Neither payload can satisfy its claimed level, and
     * both are server-generated placeholders rather than player-authored
     * book progress, so migrate only those exact tuples to the baseline. */
    if (fullSnapshot)
    {
        snprintf(query, sizeof(query),
                 "UPDATE account_role_training_books SET book_level=%u,book_experience=%u "
                 "WHERE account_id=CAST(X'%s' AS CHAR) AND "
                 "((book_level=1 AND book_experience=0) OR "
                 "(book_level=10 AND book_experience=1303))",
                 VM_NET_MOCK_TRAINING_BOOK_DEFAULT_LEVEL,
                 VM_NET_MOCK_TRAINING_BOOK_DEFAULT_EXPERIENCE, accountHex);
    }
    else
    {
        snprintf(query, sizeof(query),
                 "UPDATE account_role_training_books SET book_level=%u,book_experience=%u "
                 "WHERE account_id=CAST(X'%s' AS CHAR) AND role_id=%u AND "
                 "((book_level=1 AND book_experience=0) OR "
                 "(book_level=10 AND book_experience=1303))",
                 VM_NET_MOCK_TRAINING_BOOK_DEFAULT_LEVEL,
                 VM_NET_MOCK_TRAINING_BOOK_DEFAULT_EXPERIENCE,
                 accountHex, scopedRoleId);
    }
    if (!vm_mysql_exec(query))
        return false;

    for (u32 roleIndex = 0; roleIndex < database->roleCount; ++roleIndex)
    {
        const vm_net_mock_role_state *role = &database->roles[roleIndex];
        u8 itemCount = 0;

        if (!fullSnapshot && role->roleId != scopedRoleId)
            continue;
        itemCount = vm_net_mock_role_backpack_count(role);
        for (u32 itemIndex = 0; itemIndex < itemCount; ++itemIndex)
        {
            const vm_net_mock_backpack_item_state *item = &role->backpackItems[itemIndex];
            if (item->itemId != 921 || item->seq == 0 || item->count != 1)
                continue;
            snprintf(query, sizeof(query),
                     "INSERT IGNORE INTO account_role_training_books("
                     "account_id,role_id,item_seq,title,book_description,book_info,book_level,book_experience) "
                     "VALUES(CAST(X'%s' AS CHAR),%u,%u,X'%s',X'%s',X'%s',%u,%u)",
                     accountHex, role->roleId, item->seq, titleHex,
                     descriptionHex, descriptionHex,
                     VM_NET_MOCK_TRAINING_BOOK_DEFAULT_LEVEL,
                     VM_NET_MOCK_TRAINING_BOOK_DEFAULT_EXPERIENCE);
            if (!vm_mysql_exec(query))
                return false;
        }
    }
    return true;
}

typedef struct
{
    vm_net_mock_training_book_record *record;
} vm_net_mock_training_book_load_context;

static bool vm_net_mock_training_book_load_row(void *contextValue,
                                                unsigned int columnCount,
                                                const char *const *values,
                                                const size_t *lengths)
{
    vm_net_mock_training_book_load_context *context =
        (vm_net_mock_training_book_load_context *)contextValue;
    vm_net_mock_training_book_record *record = context ? context->record : NULL;

    if (record == NULL || record->found || columnCount != 5 || values[0] == NULL ||
        values[1] == NULL || values[2] == NULL ||
        lengths[0] == 0 || lengths[0] >= sizeof(record->title) ||
        lengths[1] == 0 || lengths[1] >= sizeof(record->description) ||
        lengths[2] == 0 || lengths[2] >= sizeof(record->bookInfo) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &record->level) ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &record->experience))
    {
        if (record != NULL)
            record->invalid = true;
        return true;
    }
    memcpy(record->title, values[0], lengths[0]);
    record->title[lengths[0]] = 0;
    memcpy(record->description, values[1], lengths[1]);
    record->description[lengths[1]] = 0;
    memcpy(record->bookInfo, values[2], lengths[2]);
    record->bookInfo[lengths[2]] = 0;
    record->found = true;
    return true;
}

static bool vm_net_mock_training_book_load_active_instance(
    vm_net_mock_role_state *role, u16 itemSeq, vm_net_mock_training_book_record *recordOut)
{
    char accountHex[129];
    char query[768];
    vm_net_mock_training_book_load_context context;
    vm_net_mock_backpack_item_state *item = NULL;

    if (recordOut == NULL || role == NULL || itemSeq == 0 ||
        !vm_net_mock_training_book_schema_prepare() ||
        !vm_net_mock_role_service_account_hex(g_vm_mock_service_active_account_id,
                                              accountHex))
    {
        return false;
    }
    item = vm_net_mock_role_find_backpack_item(role, 921, itemSeq);
    if (item == NULL || item->itemId != 921 || item->count != 1)
        return false;

    memset(recordOut, 0, sizeof(*recordOut));
    context.record = recordOut;
    snprintf(query, sizeof(query),
             "SELECT title,book_description,book_info,book_level,book_experience "
             "FROM account_role_training_books WHERE account_id=CAST(X'%s' AS CHAR) "
             "AND role_id=%u AND item_seq=%u",
             accountHex, role->roleId, itemSeq);
    if (!vm_mysql_query(query, vm_net_mock_training_book_load_row, &context) ||
        recordOut->invalid)
    {
        return false;
    }
    if (recordOut->found)
        return true;

    /* Records created before this schema existed are repaired by the same
     * transactional role-save path used by new grants, then read back. */
    if (!vm_net_mock_role_db_save("training-book-instance-backfill"))
        return false;
    memset(recordOut, 0, sizeof(*recordOut));
    context.record = recordOut;
    if (!vm_mysql_query(query, vm_net_mock_training_book_load_row, &context) ||
        recordOut->invalid || !recordOut->found)
    {
        return false;
    }
    return true;
}

/* JianghuOL.CBE:0x0100FD30 reads 7/42.booksinfo as one entry per 921 book:
 *   seq:i16-tagged, title:cstr-len16.
 * It copies the title into an 18-byte record whose last two bytes already
 * hold seq, leaving exactly 16 bytes for the NUL-terminated GBK label.  The
 * backpack renderer at mmGame:0x3AA8 then replaces item 921's local DSH name
 * with this per-instance label.  Do not put a longer title on this wire path:
 * the client has no bounds check before that copy. */
enum
{
    VM_NET_MOCK_TRAINING_BOOK_LIST_TITLE_BYTES = 16,
    VM_NET_MOCK_TRAINING_BOOK_LIST_TITLE_TEXT_BYTES =
        VM_NET_MOCK_TRAINING_BOOK_LIST_TITLE_BYTES - 1,
    VM_NET_MOCK_TRAINING_BOOK_LIST_ENTRY_MAX_BYTES =
        4 + 2 + VM_NET_MOCK_TRAINING_BOOK_LIST_TITLE_BYTES
};

static bool vm_net_mock_training_book_copy_list_title(
    const char *source,
    char destination[VM_NET_MOCK_TRAINING_BOOK_LIST_TITLE_BYTES],
    bool *truncatedOut)
{
    size_t sourcePos = 0;
    size_t destinationPos = 0;
    bool truncated = false;

    if (source == NULL || destination == NULL)
        return false;
    while (source[sourcePos] != 0)
    {
        unsigned char first = (unsigned char)source[sourcePos];
        size_t charLen = 1;

        if (first >= 0x80)
        {
            unsigned char second = (unsigned char)source[sourcePos + 1];
            if (first < 0x81 || second < 0x40 || second == 0x7f || second > 0xfe)
                return false;
            charLen = 2;
        }
        if (destinationPos + charLen > VM_NET_MOCK_TRAINING_BOOK_LIST_TITLE_TEXT_BYTES)
        {
            truncated = true;
            break;
        }
        memcpy(destination + destinationPos, source + sourcePos, charLen);
        destinationPos += charLen;
        sourcePos += charLen;
    }
    if (destinationPos == 0)
        return false;
    destination[destinationPos] = 0;
    if (truncatedOut != NULL)
        *truncatedOut = truncated;
    return true;
}

static bool vm_net_mock_build_training_book_list_blob(
    vm_net_mock_role_state *role, u8 *out, u32 outCap, u8 *bookCountOut,
    u32 *blobLenOut)
{
    u8 itemCount = vm_net_mock_role_backpack_count(role);
    u8 bookCount = 0;
    u32 pos = 0;

    if (out == NULL || bookCountOut == NULL || blobLenOut == NULL)
        return false;
    *bookCountOut = 0;
    *blobLenOut = 0;
    for (u32 itemIndex = 0; itemIndex < itemCount; ++itemIndex)
    {
        const vm_net_mock_backpack_item_state *item = &role->backpackItems[itemIndex];
        vm_net_mock_training_book_record record;
        char title[VM_NET_MOCK_TRAINING_BOOK_LIST_TITLE_BYTES];
        bool truncated = false;

        if (!vm_net_mock_backpack_item_is_client_grid_item(item) ||
            item->itemId != 921)
        {
            continue;
        }
        if (item->seq == 0 || item->count != 1 ||
            !vm_net_mock_training_book_load_active_instance(role, item->seq, &record) ||
            !vm_net_mock_training_book_copy_list_title(record.title, title, &truncated) ||
            !vm_net_mock_seq_put_i16(out, outCap, &pos, item->seq) ||
            !vm_net_mock_seq_put_string(out, outCap, &pos, title))
        {
            printf("[error][network] mock_training_book_list invalid role=%u seq=%u count=%u action=reject-7/42 evidence=JianghuOL.CBE:0x0100FD30\n",
                   role ? role->roleId : 0, item ? item->seq : 0,
                   item ? item->count : 0);
            return false;
        }
        if (truncated)
        {
            printf("[warn][network] mock_training_book_list title_truncated role=%u seq=%u max_bytes=%u\n",
                   role->roleId, item->seq,
                   (unsigned int)VM_NET_MOCK_TRAINING_BOOK_LIST_TITLE_TEXT_BYTES);
        }
        ++bookCount;
    }
    *bookCountOut = bookCount;
    *blobLenOut = pos;
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
        /* This is a read-through UI/service cache only.  The role's equipped
         * instance is the unique durable authority; persisting this cache was
         * what previously reset or detached durability after a move. */
        if (vm_net_mock_role_equipment_slot_is_usable(role, slot))
        {
            state->equipmentItemIds[slot] = role->equippedItems[slot].itemId;
            state->durability[slot] = role->equippedItems[slot].durability;
            state->durabilityMax[slot] = role->equippedItems[slot].durabilityMax;
        }
        else
        {
            state->equipmentItemIds[slot] = 0;
            state->durability[slot] = 0;
            state->durabilityMax[slot] = 0;
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
    printf("[info][network] mock_role_service_load account=%s role=%u skills=%u durability_source=role-instance invalid=%u\n",
           accountId, role->roleId, state->learnedSkillCount,
           context.invalid ? 1u : 0u);
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
        if (!vm_net_mock_role_equipment_slot_is_usable(role, slot) ||
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
    vm_net_mock_role_state before;
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
    before = *role;
    role->money -= cost;
    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        if (!vm_net_mock_role_equipment_slot_is_usable(role, slot) ||
            state->durability[slot] >= state->durabilityMax[slot])
        {
            continue;
        }
        state->durability[slot] = state->durabilityMax[slot];
        role->equippedItems[slot].durability =
            role->equippedItems[slot].durabilityMax;
    }
    if (!vm_net_mock_role_db_save("npc-equipment-repair"))
    {
        *role = before;
        vm_net_mock_role_service_sync_equipment(state, role);
        return false;
    }
    return true;
}

static void vm_net_mock_role_service_apply_battle_wear(
    vm_net_mock_role_state *role)
{
    vm_net_mock_role_service_state *state =
        vm_net_mock_role_service_state_get(role);
    vm_net_mock_role_state before;

    if (state == NULL || role == NULL || g_mockBattleOperateSessionSerial == 0 ||
        state->lastBattleWearSerial == g_mockBattleOperateSessionSerial)
    {
        return;
    }
    state->lastBattleWearSerial = g_mockBattleOperateSessionSerial;
    before = *role;
    for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        if (!vm_net_mock_role_equipment_slot_is_usable(role, slot) ||
            role->equippedItems[slot].durability == 0)
            continue;
        --role->equippedItems[slot].durability;
        state->durability[slot] = role->equippedItems[slot].durability;
    }
    if (!vm_net_mock_role_db_save("battle-equipment-durability-wear"))
    {
        *role = before;
        vm_net_mock_role_service_sync_equipment(state, role);
        printf("[error][network] mock_equipment_durability_wear_store role=%u battle=%u error=%s\n",
               role->roleId, g_mockBattleOperateSessionSerial, vm_mysql_last_error());
        return;
    }
    printf("[info][network] mock_equipment_durability_wear role=%u battle=%u amount=1\n",
           role->roleId, g_mockBattleOperateSessionSerial);

    /* 战斗心得 is a live one-hour effect.  Its resource wording promises
     * automatic repair, not a free repair: retain the existing NPC repair
     * pricing/eligibility and only repair after this battle's normal one-point
     * wear has committed.  A shortfall leaves durability unchanged and does
     * not turn an unaffordable repair into a fake success. */
    if (vm_net_mock_role_active_battle_exp_bonus_percent(role) != 0)
    {
        u16 repairCount = 0;
        u32 repairCost = 0;

        if (vm_net_mock_role_service_repair_all(role, &repairCount, &repairCost))
        {
            if (repairCount != 0)
            {
                printf("[info][network] mock_battle_insight_auto_repair role=%u battle=%u repaired=%u cost=%u action=committed\n",
                       role->roleId, g_mockBattleOperateSessionSerial,
                       repairCount, repairCost);
            }
        }
        else if (repairCount != 0)
        {
            printf("[info][network] mock_battle_insight_auto_repair role=%u battle=%u repaired=0 cost=%u money=%u action=insufficient-funds\n",
                   role->roleId, g_mockBattleOperateSessionSerial,
                   repairCost, role->money);
        }
    }
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
                                              bool equip, u32 quality, u32 category)
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
    item->quality = equip ? (u8)(quality > 255 ? 255 : quality) : 0;
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
        u32 quality = 0;
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
            else if (equip && col == 6)
                quality = vm_net_mock_parse_dsh_u32(value, valueLen, 0);
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
                                             quality,
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
                                                0,
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

/* Battle心得 may free a single physical bag row only when a rolled reward
 * cannot be inserted.  Sell the least valuable ordinary equipment instance
 * first, use the same 10% base-price rule as the explicit recovery NPC, and
 * never touch consumables, quest items, equipped instances or enhanced-price
 * guesses.  The caller retries its normal grant only after this commit. */
static bool vm_net_mock_battle_insight_auto_sell_one_equipment(
    vm_net_mock_role_state *role, u32 *soldItemIdOut, u16 *soldSeqOut,
    u32 *salePriceOut)
{
    vm_net_mock_role_state before;
    const vm_net_mock_shop_catalog_item *bestCatalog = NULL;
    vm_net_mock_backpack_item_state *bestItem = NULL;
    u32 bestPrice = 0;
    u32 bestItemId = 0;
    u16 bestItemSeq = 0;
    u32 remaining = 0;

    if (soldItemIdOut)
        *soldItemIdOut = 0;
    if (soldSeqOut)
        *soldSeqOut = 0;
    if (salePriceOut)
        *salePriceOut = 0;
    if (role == NULL ||
        vm_net_mock_role_backpack_count(role) < role->backpackCapacity)
    {
        return false;
    }
    for (u32 index = 0; index < vm_net_mock_role_backpack_count(role); ++index)
    {
        vm_net_mock_backpack_item_state *item = &role->backpackItems[index];
        const vm_net_mock_shop_catalog_item *catalog =
            vm_net_mock_find_shop_catalog_item(item->itemId);
        u32 resale = 0;

        if (item->itemId == 0 || item->seq == 0 || item->count != 1 ||
            catalog == NULL || !catalog->isEquip || catalog->price == 0)
        {
            continue;
        }
        resale = (catalog->price / 100u) *
                     VM_NET_MOCK_NPC_SERVICE_EQUIPMENT_SELL_PERCENT +
                 (((catalog->price % 100u) *
                       VM_NET_MOCK_NPC_SERVICE_EQUIPMENT_SELL_PERCENT +
                   99u) /
                  100u);
        if (bestItem == NULL || resale < bestPrice ||
            (resale == bestPrice && item->seq < bestItem->seq))
        {
            bestItem = item;
            bestCatalog = catalog;
            bestPrice = resale;
            bestItemId = item->itemId;
            bestItemSeq = item->seq;
        }
    }
    if (bestItem == NULL || bestCatalog == NULL || bestPrice == 0)
        return false;

    before = *role;
    if (!vm_net_mock_role_consume_backpack_item(role, bestItemId,
                                                 bestItemSeq, 1, &remaining) ||
        remaining != 0)
    {
        *role = before;
        return false;
    }
    role->money = 0xffffffffu - role->money < bestPrice ?
                      0xffffffffu : role->money + bestPrice;
    if (!vm_net_mock_role_db_save("battle-insight-auto-sell"))
    {
        *role = before;
        return false;
    }
    if (soldItemIdOut)
        *soldItemIdOut = bestCatalog->itemId;
    if (soldSeqOut)
        *soldSeqOut = bestItemSeq;
    if (salePriceOut)
        *salePriceOut = bestPrice;
    return true;
}

static int vm_net_mock_chest_kind_index(u32 chestItemId)
{
    for (u32 i = 0; i < VM_NET_MOCK_CHEST_KIND_COUNT; ++i)
    {
        if (g_vm_net_mock_chest_kinds[i].chestItemId == chestItemId)
            return (int)i;
    }
    return -1;
}

static void vm_net_mock_chest_rows_reset_to_identities(void)
{
    memset(g_vm_net_mock_chest_rows, 0, sizeof(g_vm_net_mock_chest_rows));
    for (u32 i = 0; i < VM_NET_MOCK_CHEST_KIND_COUNT; ++i)
    {
        g_vm_net_mock_chest_rows[i].chestItemId =
            g_vm_net_mock_chest_kinds[i].chestItemId;
        g_vm_net_mock_chest_rows[i].keyItemId =
            g_vm_net_mock_chest_kinds[i].keyItemId;
    }
}

typedef struct
{
    u32 loaded;
    u32 skipped;
    bool invalid;
} vm_net_mock_chest_db_load_context;

typedef struct
{
    bool found;
    bool invalid;
} vm_net_mock_chest_schema_column_context;

static bool vm_net_mock_chest_schema_column_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_chest_schema_column_context *context =
        (vm_net_mock_chest_schema_column_context *)contextValue;

    if (context == NULL || context->found || columnCount != 1 ||
        values == NULL || lengths == NULL || values[0] == NULL ||
        lengths[0] == 0)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

/* Existing deployments already have the chest table.  CREATE TABLE IF NOT
 * EXISTS cannot add the new flag retrospectively, so make the migration
 * explicit and MySQL-5.x compatible before selecting the six-column row. */
static bool vm_net_mock_chest_admin_ensure_world_broadcast_column(void)
{
    vm_net_mock_chest_schema_column_context context;

    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(
            "SELECT COLUMN_NAME FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='server_chest_rewards' "
            "AND COLUMN_NAME='world_broadcast'",
            vm_net_mock_chest_schema_column_row, &context) ||
        context.invalid)
    {
        return false;
    }
    if (context.found)
        return true;
    if (!vm_mysql_exec(
            "ALTER TABLE server_chest_rewards ADD COLUMN world_broadcast "
            "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER weight"))
    {
        return false;
    }
    printf("[info][mock-admin] chest_reward_schema migration=world-broadcast action=applied\n");
    return true;
}

static bool vm_net_mock_chest_db_row(void *contextValue,
                                     unsigned int columnCount,
                                     const char *const *values,
                                     const size_t *lengths)
{
    vm_net_mock_chest_db_load_context *context =
        (vm_net_mock_chest_db_load_context *)contextValue;
    u32 chestItemId = 0;
    u32 rewardOrder = 0;
    u32 itemId = 0;
    u32 count = 0;
    u32 weight = 0;
    u32 worldBroadcast = 0;
    int chestIndex = -1;
    vm_net_mock_chest_admin_row *chest = NULL;

    if (context == NULL || columnCount != 6 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &chestItemId) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &rewardOrder) ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &itemId) ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &count) ||
        !vm_mock_mysql_parse_u32(values[4], lengths[4], &weight) ||
        !vm_mock_mysql_parse_u32(values[5], lengths[5], &worldBroadcast) ||
        (chestIndex = vm_net_mock_chest_kind_index(chestItemId)) < 0 ||
        rewardOrder == 0 || rewardOrder > VM_NET_MOCK_CHEST_REWARD_MAX ||
        itemId == 0 || count == 0 ||
        count > VM_NET_MOCK_CHEST_REWARD_COUNT_MAX || weight == 0 ||
        weight > VM_NET_MOCK_CHEST_REWARD_WEIGHT_MAX || worldBroadcast > 1)
    {
        if (context != NULL)
        {
            ++context->skipped;
            context->invalid = true;
        }
        return true;
    }

    chest = &g_vm_net_mock_chest_rows[chestIndex];
    /* The persisted ordering is part of the configured probability contract.
     * Gaps or duplicate slots would make an edited table ambiguous, so fail
     * closed instead of silently changing the drawn distribution. */
    if (rewardOrder != (u32)chest->rewardCount + 1u)
    {
        ++context->skipped;
        context->invalid = true;
        return true;
    }
    for (u8 i = 0; i < chest->rewardCount; ++i)
    {
        if (chest->rewards[i].itemId == itemId)
        {
            ++context->skipped;
            context->invalid = true;
            return true;
        }
    }
    chest->rewards[chest->rewardCount].itemId = itemId;
    chest->rewards[chest->rewardCount].count = count;
    chest->rewards[chest->rewardCount].weight = weight;
    chest->rewards[chest->rewardCount].worldBroadcast =
        worldBroadcast ? 1 : 0;
    ++chest->rewardCount;
    ++context->loaded;
    return true;
}

static bool vm_net_mock_chest_admin_db_load(void)
{
    vm_net_mock_chest_db_load_context context;

    if (g_vm_net_mock_chest_db_loaded)
        return g_vm_net_mock_chest_db_valid;
    g_vm_net_mock_chest_db_loaded = true;
    g_vm_net_mock_chest_db_valid = false;
    memset(&context, 0, sizeof(context));
    vm_net_mock_chest_rows_reset_to_identities();

    if (!vm_mysql_exec(
            "CREATE TABLE IF NOT EXISTS server_chest_rewards ("
            "chest_item_id INT UNSIGNED NOT NULL,"
            "reward_order TINYINT UNSIGNED NOT NULL,"
            "item_id INT UNSIGNED NOT NULL,"
            "item_count INT UNSIGNED NOT NULL,"
            "weight INT UNSIGNED NOT NULL,"
            "world_broadcast TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(chest_item_id,reward_order),"
            "KEY idx_server_chest_rewards_item(item_id)) ENGINE=InnoDB") ||
        !vm_net_mock_chest_admin_ensure_world_broadcast_column() ||
        !vm_mysql_query(
            "SELECT chest_item_id,reward_order,item_id,item_count,weight,world_broadcast "
            "FROM server_chest_rewards ORDER BY chest_item_id,reward_order",
            vm_net_mock_chest_db_row, &context) ||
        context.invalid)
    {
        vm_net_mock_chest_rows_reset_to_identities();
        printf("[error][mock-admin] chest_reward_db_load failed rows=%u skipped=%u error=%s\n",
               context.loaded, context.skipped, vm_mysql_last_error());
        return false;
    }

    g_vm_net_mock_chest_db_valid = true;
    printf("[info][mock-admin] chest_reward_db_load rows=%u\n", context.loaded);
    return true;
}

static u32 vm_net_mock_chest_admin_list(vm_net_mock_chest_admin_row *rows,
                                        u32 rowCap)
{
    if (!vm_net_mock_chest_admin_db_load())
        return 0;
    if (rows != NULL && rowCap != 0)
    {
        u32 copied = rowCap < VM_NET_MOCK_CHEST_KIND_COUNT
                         ? rowCap : VM_NET_MOCK_CHEST_KIND_COUNT;
        memcpy(rows, g_vm_net_mock_chest_rows, sizeof(*rows) * copied);
    }
    return VM_NET_MOCK_CHEST_KIND_COUNT;
}

static bool vm_net_mock_chest_admin_save(
    const vm_net_mock_chest_admin_row *row, const char **errorOut)
{
    char query[512];
    char mysqlError[512];
    int chestIndex = -1;
    bool transactionStarted = false;

    if (errorOut)
        *errorOut = "宝箱奖池参数无效";
    if (row == NULL || row->rewardCount == 0 ||
        row->rewardCount > VM_NET_MOCK_CHEST_REWARD_MAX ||
        (chestIndex = vm_net_mock_chest_kind_index(row->chestItemId)) < 0 ||
        row->keyItemId != g_vm_net_mock_chest_kinds[chestIndex].keyItemId)
    {
        return false;
    }
    for (u8 i = 0; i < row->rewardCount; ++i)
    {
        const vm_net_mock_chest_reward *reward = &row->rewards[i];

        if (reward->itemId == 0 || reward->count == 0 ||
            reward->count > VM_NET_MOCK_CHEST_REWARD_COUNT_MAX ||
            reward->weight == 0 ||
            reward->weight > VM_NET_MOCK_CHEST_REWARD_WEIGHT_MAX ||
            reward->worldBroadcast > 1 ||
            vm_net_mock_find_shop_catalog_item(reward->itemId) == NULL)
        {
            if (errorOut)
                *errorOut = "奖池物品、数量或权重无效";
            return false;
        }
        if ((vm_net_mock_find_equipment_catalog_item(reward->itemId) != NULL ||
             vm_net_mock_backpack_item_id_uses_reservoir_count(reward->itemId)) &&
            reward->count != 1)
        {
            if (errorOut)
                *errorOut = "装备、神仙壶和逍遥壶每次只能开出 1 个";
            return false;
        }
        for (u8 previous = 0; previous < i; ++previous)
        {
            if (row->rewards[previous].itemId == reward->itemId)
            {
                if (errorOut)
                    *errorOut = "同一宝箱不能重复配置相同物品";
                return false;
            }
        }
    }
    if (!g_vm_net_mock_chest_db_valid)
    {
        g_vm_net_mock_chest_db_loaded = false;
        if (!vm_net_mock_chest_admin_db_load())
        {
            if (errorOut)
                *errorOut = "宝箱奖池数据库不可用";
            return false;
        }
    }

    if (!vm_mysql_exec("START TRANSACTION"))
        goto mysql_failed;
    transactionStarted = true;
    snprintf(query, sizeof(query),
             "DELETE FROM server_chest_rewards WHERE chest_item_id=%u",
             row->chestItemId);
    if (!vm_mysql_exec(query))
        goto mysql_failed;
    for (u8 i = 0; i < row->rewardCount; ++i)
    {
        const vm_net_mock_chest_reward *reward = &row->rewards[i];

        snprintf(query, sizeof(query),
                 "INSERT INTO server_chest_rewards("
                 "chest_item_id,reward_order,item_id,item_count,weight,world_broadcast) "
                 "VALUES(%u,%u,%u,%u,%u,%u)",
                 row->chestItemId, (u32)i + 1u, reward->itemId,
                 reward->count, reward->weight,
                 reward->worldBroadcast ? 1u : 0u);
        if (!vm_mysql_exec(query))
            goto mysql_failed;
    }
    if (!vm_mysql_exec("COMMIT"))
        goto mysql_failed;
    transactionStarted = false;
    g_vm_net_mock_chest_rows[chestIndex] = *row;
    g_vm_net_mock_chest_rows[chestIndex].keyItemId =
        g_vm_net_mock_chest_kinds[chestIndex].keyItemId;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] chest_reward_save chest=%u key=%u rows=%u\n",
           row->chestItemId, row->keyItemId, row->rewardCount);
    return true;

mysql_failed:
    snprintf(mysqlError, sizeof(mysqlError), "%s", vm_mysql_last_error());
    if (transactionStarted)
        (void)vm_mysql_exec("ROLLBACK");
    printf("[error][mock-admin] chest_reward_save_failed chest=%u error=%s\n",
           row ? row->chestItemId : 0, mysqlError);
    if (errorOut)
        *errorOut = "宝箱奖池保存失败，请检查 MySQL 日志";
    return false;
}

static bool vm_net_mock_chest_admin_reset(u32 chestItemId,
                                          const char **errorOut)
{
    char query[256];
    int chestIndex = vm_net_mock_chest_kind_index(chestItemId);

    if (errorOut)
        *errorOut = "宝箱类型无效";
    if (chestIndex < 0)
        return false;
    if (!g_vm_net_mock_chest_db_valid)
    {
        g_vm_net_mock_chest_db_loaded = false;
        if (!vm_net_mock_chest_admin_db_load())
        {
            if (errorOut)
                *errorOut = "宝箱奖池数据库不可用";
            return false;
        }
    }
    snprintf(query, sizeof(query),
             "DELETE FROM server_chest_rewards WHERE chest_item_id=%u",
             chestItemId);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = "清空宝箱奖池失败，请检查 MySQL 日志";
        return false;
    }
    memset(&g_vm_net_mock_chest_rows[chestIndex], 0,
           sizeof(g_vm_net_mock_chest_rows[chestIndex]));
    g_vm_net_mock_chest_rows[chestIndex].chestItemId = chestItemId;
    g_vm_net_mock_chest_rows[chestIndex].keyItemId =
        g_vm_net_mock_chest_kinds[chestIndex].keyItemId;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] chest_reward_reset chest=%u\n", chestItemId);
    return true;
}

typedef struct
{
    u32 loaded;
    u32 skipped;
} vm_net_mock_shop_admin_load_context;

typedef struct
{
    bool found;
    bool invalid;
} vm_net_mock_shop_admin_schema_column_context;

static bool vm_net_mock_shop_admin_schema_column_row(
    void *contextValue, unsigned int columnCount, const char *const *values,
    const size_t *lengths)
{
    vm_net_mock_shop_admin_schema_column_context *context =
        (vm_net_mock_shop_admin_schema_column_context *)contextValue;

    if (context == NULL || context->found || columnCount != 1 ||
        values[0] == NULL || lengths[0] == 0)
    {
        if (context != NULL)
            context->invalid = true;
        return true;
    }
    context->found = true;
    return true;
}

/* Existing deployments already have server_shop_items.  Create-table cannot
 * add this column retrospectively, so probe information_schema before the
 * portable MySQL-5.x ALTER. */
static bool vm_net_mock_shop_admin_ensure_section_column(void)
{
    vm_net_mock_shop_admin_schema_column_context context;

    memset(&context, 0, sizeof(context));
    if (!vm_mysql_query(
            "SELECT COLUMN_NAME FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='server_shop_items' "
            "AND COLUMN_NAME='shop_section'",
            vm_net_mock_shop_admin_schema_column_row, &context) ||
        context.invalid)
    {
        return false;
    }
    if (context.found)
        return true;
    if (!vm_mysql_exec(
            "ALTER TABLE server_shop_items ADD COLUMN shop_section "
            "TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER enabled"))
    {
        return false;
    }
    printf("[info][mock-admin] shop_item_schema migration=shop-section action=applied\n");
    return true;
}

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
    u32 shopSection = 0;
    vm_net_mock_shop_catalog_item *item = NULL;

    if (context == NULL || columnCount != 4 ||
        !vm_mock_mysql_parse_u32(values[0], lengths[0], &itemId) ||
        !vm_mock_mysql_parse_u32(values[1], lengths[1], &price) || price == 0 ||
        !vm_mock_mysql_parse_u32(values[2], lengths[2], &enabled) || enabled > 1 ||
        !vm_mock_mysql_parse_u32(values[3], lengths[3], &shopSection) ||
        shopSection > VM_NET_MOCK_SHOP_SECTION_NORMAL)
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
    item->shopSection = (u8)shopSection;
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
            "shop_section TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "PRIMARY KEY(item_id)) ENGINE=InnoDB") ||
        !vm_net_mock_shop_admin_ensure_section_column() ||
        !vm_mysql_query(
            "SELECT item_id,price,enabled,shop_section FROM server_shop_items ORDER BY item_id",
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
                                        u8 shopSection, const char **errorOut)
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
    if (itemId == 0 || price == 0 ||
        shopSection > VM_NET_MOCK_SHOP_SECTION_NORMAL)
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
    if (item->isEquip && shopSection != VM_NET_MOCK_SHOP_SECTION_AUTO)
    {
        if (errorOut)
            *errorOut = "装备商城分区由穿戴部位决定";
        return false;
    }
    snprintf(query, sizeof(query),
             "INSERT INTO server_shop_items(item_id,price,enabled,shop_section) VALUES(%u,%u,%u,%u) "
             "ON DUPLICATE KEY UPDATE price=VALUES(price),enabled=VALUES(enabled),"
             "shop_section=VALUES(shop_section)",
             itemId, price, enabled ? 1u : 0u, shopSection);
    if (!vm_mysql_exec(query))
    {
        if (errorOut)
            *errorOut = vm_mysql_last_error();
        return false;
    }
    item->price = price;
    item->enabled = enabled ? 1 : 0;
    item->shopSection = shopSection;
    if (errorOut)
        *errorOut = "ok";
    printf("[info][mock-admin] shop_item_save item=%u price=%u enabled=%u section=%u\n",
           itemId, price, enabled ? 1u : 0u, shopSection);
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
                                                   u32 quality, u32 category, u32 durabilityMax,
                                                   const vm_net_mock_equipment_bonus *bonus)
{
    vm_net_mock_equipment_catalog_item *item = NULL;
    u8 slot = vm_net_mock_equipment_slot_for_category(category);

    if (itemId == 0 || quality > 0xffu || category > 0xffu ||
        durabilityMax == 0 || durabilityMax > 0xffffu ||
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
    item->quality = (u8)quality;
    item->category = (u8)category;
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
        u32 quality = 0xffu;
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
                                                   category,
                                                   durabilityMax, &bonus))
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
 * ParseEquipAttributes (JianghuOL.CBE:0x010185C2) stores the first two
 * i16 values in the wire extension at item+286 and item+287 respectively.
 * The client renders item+286 as "+%d" and uses it for equipment stat
 * thresholds; item+287 is the enhancement ceiling.  These fields are not a
 * stack-count extension.  Keep the cap derived from the same equip.dsh
 * classification that validates actual equipment instances.
 */
static u8 vm_net_mock_item_common_extra_enhance_cap(u32 itemId)
{
    return vm_net_mock_find_equipment_catalog_item(itemId) != NULL
               ? VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL
               : 0;
}

static u16 vm_net_mock_equipment_durability_max_for_item(u32 itemId)
{
    const vm_net_mock_equipment_catalog_item *item =
        vm_net_mock_find_equipment_catalog_item(itemId);

    return item != NULL ? item->durabilityMax : 0;
}

/*
 * Enhancement attribute contract
 * ------------------------------
 *
 * scene_rebuild_status_meter_node(0x0100FED8) calls
 * CalcEquipStatBonus(0x01028B34) for the physical attack of a weapon and
 * for the armour of every other equipment slot.  The client calculator adds
 * one entry per enhancement level:
 *
 *     flat_u8 + floor(percent_i16 * base_stat / 100)
 *
 * The following sixteen entries are recovered from that calculator and from
 * independent +4/+12/+16 equipment samples.  In particular they reproduce
 * 武林神胫 1653 armour -> 2327 at +4, 绒丝袍 1122 armour -> 3024 at +12,
 * 圣诞魔杖 374 attack -> 1076 at +12, and 梦境魔杖 529 attack -> 2103 at
 * +16.  They are not the enhancement-screen material/price rows.
 */
typedef struct
{
    u8 flat;
    u16 percent;
} vm_net_mock_equipment_enhance_primary_rule;

static const vm_net_mock_equipment_enhance_primary_rule
    g_vm_net_mock_equipment_enhance_primary_rules[
        VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL] = {
            {2, 10}, {3, 10}, {4, 10}, {5, 10},
            {7, 14}, {8, 16}, {9, 14}, {10, 16},
            {14, 14}, {15, 16}, {16, 14}, {17, 16},
            {20, 22}, {23, 24}, {26, 26}, {28, 28}};

/* mmTitleMstarWqvga.cbm:
 * title_parse_equipment_enhance_primary_rules(0x1568) owns the native
 * itemCtrl+0x584 table.  The title role-list subtype-4 parser reads `num`,
 * opens `data` as a tagged stream, then consumes exactly one u8 flat value
 * followed by one i16 percentage value per level.  Keep this serializer next
 * to the authoritative curve so the title packet and server-side stat
 * aggregation cannot silently diverge. */
static u32 vm_net_mock_build_equipment_enhance_primary_rule_data(
    u8 *out, u32 outCap)
{
    u32 pos = 0;

    if (out == NULL)
        return 0;
    for (u8 i = 0; i < VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL; ++i)
    {
        const vm_net_mock_equipment_enhance_primary_rule *rule =
            &g_vm_net_mock_equipment_enhance_primary_rules[i];
        if (!vm_net_mock_seq_put_u8(out, outCap, &pos, rule->flat) ||
            !vm_net_mock_seq_put_i16(out, outCap, &pos, rule->percent))
        {
            return 0;
        }
    }
    return pos;
}

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
    VM_NET_MOCK_EQUIP_ATTR_MP = 10
};

typedef struct
{
    u8 threshold;
    u8 type;
    u8 mode;
    u16 value;
} vm_net_mock_equipment_enhance_attr;

static u16 vm_net_mock_equipment_enhance_attr_value(u32 value)
{
    return (u16)SDL_min(value, 0x7fffu);
}

static u16 vm_net_mock_equipment_enhance_stage_value(u8 type,
                                                       u8 levelRequired)
{
    u32 level = levelRequired == 0 ? 1u : levelRequired;

    switch (type)
    {
    case VM_NET_MOCK_EQUIP_ATTR_ATTACK:
        return vm_net_mock_equipment_enhance_attr_value(level * 2u -
                                                         (level > 3 ? 6u : 0u));
    case VM_NET_MOCK_EQUIP_ATTR_ARMOR:
        return vm_net_mock_equipment_enhance_attr_value(level * 6u + 135u);
    case VM_NET_MOCK_EQUIP_ATTR_HP:
        return vm_net_mock_equipment_enhance_attr_value(level * 8u + 20u);
    case VM_NET_MOCK_EQUIP_ATTR_MP:
        return vm_net_mock_equipment_enhance_attr_value(level * 6u + 30u);
    case VM_NET_MOCK_EQUIP_ATTR_WISDOM:
        return vm_net_mock_equipment_enhance_attr_value(level * 2u + 10u);
    case VM_NET_MOCK_EQUIP_ATTR_CRIT:
    case VM_NET_MOCK_EQUIP_ATTR_HIT:
    case VM_NET_MOCK_EQUIP_ATTR_DODGE:
        return (u16)(1u + (level + 10u) / 20u);
    default:
        return 0;
    }
}

static void vm_net_mock_equipment_enhancement_stage_types(u8 slot,
                                                           u8 out[4])
{
    static const u8 kStageTypes[VM_NET_MOCK_EQUIP_SLOT_COUNT][4] = {
        /* weapon */ {VM_NET_MOCK_EQUIP_ATTR_CRIT, VM_NET_MOCK_EQUIP_ATTR_ATTACK,
                      VM_NET_MOCK_EQUIP_ATTR_HIT, VM_NET_MOCK_EQUIP_ATTR_WISDOM},
        /* helmet */ {VM_NET_MOCK_EQUIP_ATTR_WISDOM, VM_NET_MOCK_EQUIP_ATTR_ARMOR,
                      VM_NET_MOCK_EQUIP_ATTR_DODGE, VM_NET_MOCK_EQUIP_ATTR_MP},
        /* chest  */ {VM_NET_MOCK_EQUIP_ATTR_MP, VM_NET_MOCK_EQUIP_ATTR_ARMOR,
                      VM_NET_MOCK_EQUIP_ATTR_HP, VM_NET_MOCK_EQUIP_ATTR_DODGE},
        /* cloak  */ {VM_NET_MOCK_EQUIP_ATTR_CRIT, VM_NET_MOCK_EQUIP_ATTR_HIT,
                      VM_NET_MOCK_EQUIP_ATTR_DODGE, VM_NET_MOCK_EQUIP_ATTR_HP},
        /* belt   */ {VM_NET_MOCK_EQUIP_ATTR_HP, VM_NET_MOCK_EQUIP_ATTR_ARMOR,
                      VM_NET_MOCK_EQUIP_ATTR_HIT, VM_NET_MOCK_EQUIP_ATTR_MP},
        /* leg    */ {VM_NET_MOCK_EQUIP_ATTR_ATTACK, VM_NET_MOCK_EQUIP_ATTR_ARMOR,
                      VM_NET_MOCK_EQUIP_ATTR_HIT, VM_NET_MOCK_EQUIP_ATTR_HP},
        /* boot   */ {VM_NET_MOCK_EQUIP_ATTR_DODGE, VM_NET_MOCK_EQUIP_ATTR_HIT,
                      VM_NET_MOCK_EQUIP_ATTR_ARMOR, VM_NET_MOCK_EQUIP_ATTR_HP},
        /* ring   */ {VM_NET_MOCK_EQUIP_ATTR_ATTACK, VM_NET_MOCK_EQUIP_ATTR_HP,
                      VM_NET_MOCK_EQUIP_ATTR_CRIT, VM_NET_MOCK_EQUIP_ATTR_HIT}};

    if (out == NULL)
        return;
    if (slot >= VM_NET_MOCK_EQUIP_SLOT_COUNT)
        slot = 0;
    memcpy(out, kStageTypes[slot], sizeof(kStageTypes[slot]));
}

/* Enhancement stage attributes are item-instance rolls.  They are generated
 * once, saved with that instance, and then reused for rendering and battle
 * calculation.  Never generate here while serializing a response: that would
 * make opening the backpack reroll the equipment. */
static u32 g_vm_net_mock_equipment_enhance_affix_rng = 0;

static u32 vm_net_mock_equipment_enhancement_affix_rand(u32 salt)
{
    /* The service can process independent clients concurrently.  The nonce
     * is only entropy for a result that will be persisted immediately, but it
     * must still be acquired atomically so two simultaneous +4 successes do
     * not race on shared state. */
    u32 value = __atomic_add_fetch(&g_vm_net_mock_equipment_enhance_affix_rng,
                                   0x6d2b79f5u, __ATOMIC_RELAXED);

    value ^= salt ^ g_schedulerTick;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value ? value : 0x9e3779b9u;
}

static int vm_net_mock_equipment_enhancement_candidate_index(
    const u8 candidates[4], u8 type)
{
    if (candidates == NULL || type == 0)
        return -1;
    for (u8 index = 0; index < 4; ++index)
    {
        if (candidates[index] == type)
            return index;
    }
    return -1;
}

static u16 vm_net_mock_equipment_enhancement_roll_stage_value(
    u16 baseValue, u32 randomValue)
{
    u32 spread = baseValue / 10u;
    u32 value = 0;

    /* Keep the result close to the catalogue-derived base even for the small
     * integer stats such as crit/hit/dodge.  Their one-point variation is the
     * narrowest representable value range on the wire. */
    if (baseValue == 0)
        return 0;
    if (spread == 0)
        spread = 1;
    value = baseValue > spread ? baseValue - spread : 1u;
    value += randomValue % (spread * 2u + 1u);
    return vm_net_mock_equipment_enhance_attr_value(value);
}

/* Returns true when a legacy/corrupt state was repaired.  `identity` is only
 * an entropy salt: the generated result is immediately stored, so later
 * moves, relogins and displays never depend on it again. */
static bool vm_net_mock_equipment_enhancement_ensure_affixes(
    const vm_net_mock_equipment_catalog_item *equipment,
    u8 enhanceLevel,
    vm_net_mock_equipment_enhance_affix_state *affixes,
    u32 identity)
{
    u8 candidates[4];
    u8 usedCandidates = 0;
    bool changed = false;

    if (equipment == NULL || affixes == NULL)
        return false;
    /* The client only receives the complete attr-count/array when an
     * equipment instance is first created.  29/3 later changes just its
     * current enhance level, so all four future-stage rolls must already be
     * stable here; do not erase them merely because their threshold is not
     * reached yet. */
    (void)enhanceLevel;
    vm_net_mock_equipment_enhancement_stage_types(equipment->slot, candidates);

    for (u8 stage = 0; stage < 4; ++stage)
    {
        int candidateIndex = vm_net_mock_equipment_enhancement_candidate_index(
            candidates, affixes->type[stage]);

        if (candidateIndex < 0 || affixes->value[stage] == 0 ||
            (usedCandidates & (u8)(1u << candidateIndex)) != 0)
        {
            affixes->type[stage] = 0;
            affixes->value[stage] = 0;
            changed = true;
            continue;
        }
        usedCandidates |= (u8)(1u << candidateIndex);
    }

    for (u8 stage = 0; stage < 4; ++stage)
    {
        u8 threshold = (u8)((stage + 1u) * 4u);
        u8 available[4];
        u8 availableCount = 0;
        u8 candidateIndex = 0;
        u32 randomValue = 0;
        u16 baseValue = 0;

        if (affixes->type[stage] != 0)
            continue;
        for (u8 index = 0; index < 4; ++index)
        {
            if ((usedCandidates & (u8)(1u << index)) == 0)
                available[availableCount++] = index;
        }
        if (availableCount == 0)
            return changed;
        randomValue = vm_net_mock_equipment_enhancement_affix_rand(
            identity ^ equipment->itemId ^ ((u32)(stage + 1u) * 0x45d9f3bu));
        candidateIndex = available[randomValue % availableCount];
        baseValue = vm_net_mock_equipment_enhance_stage_value(
            candidates[candidateIndex], equipment->levelRequired);
        affixes->type[stage] = candidates[candidateIndex];
        affixes->value[stage] = vm_net_mock_equipment_enhancement_roll_stage_value(
            baseValue, vm_net_mock_equipment_enhancement_affix_rand(
                           identity ^ equipment->itemId ^
                           ((u32)(stage + 1u) * 0x27d4eb2du)));
        if (affixes->value[stage] == 0)
        {
            affixes->type[stage] = 0;
            return changed;
        }
        usedCandidates |= (u8)(1u << candidateIndex);
        changed = true;
    }
    return changed;
}

/* The two relational columns keep all four stage rolls in one equipment row:
 * types occupy four bytes; values occupy four unsigned 16-bit lanes.  The
 * client wire also has fixed four-stage capacity, so no variable blob or
 * lossy catalog reconstruction is involved. */
static u32 vm_net_mock_equipment_enhancement_pack_affix_types(
    const vm_net_mock_equipment_enhance_affix_state *affixes)
{
    u32 packed = 0;

    if (affixes == NULL)
        return 0;
    for (u8 stage = 0; stage < 4; ++stage)
        packed |= (u32)affixes->type[stage] << (stage * 8u);
    return packed;
}

static uint64_t vm_net_mock_equipment_enhancement_pack_affix_values(
    const vm_net_mock_equipment_enhance_affix_state *affixes)
{
    uint64_t packed = 0;

    if (affixes == NULL)
        return 0;
    for (u8 stage = 0; stage < 4; ++stage)
        packed |= (uint64_t)affixes->value[stage] << (stage * 16u);
    return packed;
}

static void vm_net_mock_equipment_enhancement_unpack_affixes(
    vm_net_mock_equipment_enhance_affix_state *affixes,
    u32 packedTypes, uint64_t packedValues)
{
    if (affixes == NULL)
        return;
    memset(affixes, 0, sizeof(*affixes));
    for (u8 stage = 0; stage < 4; ++stage)
    {
        affixes->type[stage] = (u8)(packedTypes >> (stage * 8u));
        affixes->value[stage] = (u16)(packedValues >> (stage * 16u));
    }
}

/* Match the native client calculator exactly.  Equipment categories 7/8/9
 * (sword, dagger and staff) strengthen attack; categories 0..6 strengthen
 * armour.  The title rule table carries only flat/percent rows and provides
 * no attribute selector, so the authoritative server must not infer another
 * primary from the remaining equip.dsh fields. */
static u32 vm_net_mock_equipment_enhancement_bonus_from_base(u32 base,
                                                              u8 enhanceLevel)
{
    u32 result = 0;
    u8 levels = (u8)SDL_min(enhanceLevel,
                            VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL);

    if (base == 0 || levels == 0)
        return 0;
    for (u8 i = 0; i < levels; ++i)
    {
        const vm_net_mock_equipment_enhance_primary_rule *rule =
            &g_vm_net_mock_equipment_enhance_primary_rules[i];
        result += (u32)rule->flat + (base * (u32)rule->percent) / 100u;
    }
    return result;
}

static u8 vm_net_mock_equipment_enhancement_collect_attrs(
    const vm_net_mock_equipment_catalog_item *equipment,
    u8 enhanceLevel,
    const vm_net_mock_equipment_enhance_affix_state *affixes,
    vm_net_mock_equipment_enhance_attr *out,
    u8 outCap)
{
    u8 count = 0;
    u8 effectiveLevel = (u8)SDL_min(
        enhanceLevel, VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL);

    if (equipment == NULL || affixes == NULL || out == NULL || outCap == 0)
        return 0;
    for (u8 stage = 0; stage < 4 && count < outCap; ++stage)
    {
        u8 threshold = (u8)((stage + 1u) * 4u);

        if (effectiveLevel < threshold || affixes->type[stage] == 0 ||
            affixes->value[stage] == 0)
        {
            continue;
        }
        out[count].threshold = (u8)((stage + 1u) * 4u);
        out[count].type = affixes->type[stage];
        out[count].mode = 0;
        out[count].value = affixes->value[stage];
        ++count;
    }

    return count;
}

/* `29/3` updates only item+286/item+287.  The later threshold comparisons
 * therefore require the complete +4/+8/+12/+16 plan to exist in the client
 * object before its first successful strengthen.  Append future stage rows
 * without duplicating stages already unlocked in the normal stat collector. */
static u8 vm_net_mock_equipment_enhancement_collect_wire_attrs(
    const vm_net_mock_equipment_catalog_item *equipment,
    u8 enhanceLevel,
    const vm_net_mock_equipment_enhance_affix_state *affixes,
    vm_net_mock_equipment_enhance_attr *out,
    u8 outCap)
{
    u8 count = vm_net_mock_equipment_enhancement_collect_attrs(
        equipment, enhanceLevel, affixes, out, outCap);

    if (affixes == NULL || out == NULL)
        return count;
    for (u8 stage = 0; stage < 4 && count < outCap; ++stage)
    {
        const u8 threshold = (u8)((stage + 1u) * 4u);
        bool present = false;

        if (affixes->type[stage] == 0 || affixes->value[stage] == 0)
            continue;
        for (u8 i = 0; i < count; ++i)
        {
            if (out[i].threshold == threshold)
            {
                present = true;
                break;
            }
        }
        if (present)
            continue;
        out[count].threshold = threshold;
        out[count].type = affixes->type[stage];
        out[count].mode = 0;
        out[count].value = affixes->value[stage];
        ++count;
    }
    return count;
}

static bool vm_net_mock_equipment_enhancement_resolve_primary(
    const vm_net_mock_equipment_catalog_item *equipment, u8 *typeOut,
    u32 *baseOut)
{
    u8 type = 0;
    u32 base = 0;

    if (equipment == NULL)
        return false;
    if (equipment->category >= 7 && equipment->category <= 9)
    {
        type = VM_NET_MOCK_EQUIP_ATTR_ATTACK;
        base = equipment->bonus.attack;
    }
    else if (equipment->category <= 6)
    {
        type = VM_NET_MOCK_EQUIP_ATTR_ARMOR;
        base = equipment->bonus.armor;
    }
    if (base == 0)
        return false;
    if (typeOut)
        *typeOut = type;
    if (baseOut)
        *baseOut = base;
    return true;
}

static void vm_net_mock_equipment_enhancement_add_attr_bonus(
    vm_net_mock_equipment_bonus *bonus, u8 type, u32 value)
{
    if (bonus == NULL || value == 0)
        return;
    switch (type)
    {
    case VM_NET_MOCK_EQUIP_ATTR_STRENGTH:
        bonus->strength += value;
        break;
    case VM_NET_MOCK_EQUIP_ATTR_AGILITY:
        bonus->agility += value;
        break;
    case VM_NET_MOCK_EQUIP_ATTR_WISDOM:
        bonus->wisdom += value;
        break;
    case VM_NET_MOCK_EQUIP_ATTR_ATTACK:
        bonus->attack += value;
        break;
    case VM_NET_MOCK_EQUIP_ATTR_ARMOR:
        bonus->armor += value;
        break;
    case VM_NET_MOCK_EQUIP_ATTR_DODGE:
        bonus->dodge += value;
        break;
    case VM_NET_MOCK_EQUIP_ATTR_HIT:
        bonus->hit += value;
        break;
    case VM_NET_MOCK_EQUIP_ATTR_CRIT:
        bonus->crit += value;
        break;
    case VM_NET_MOCK_EQUIP_ATTR_HP:
        bonus->hp += value;
        break;
    case VM_NET_MOCK_EQUIP_ATTR_MP:
        bonus->mp += value;
        break;
    default:
        break;
    }
}

static void vm_net_mock_equipment_enhancement_add_bonus(
    const vm_net_mock_equipment_catalog_item *equipment,
    u8 enhanceLevel,
    const vm_net_mock_equipment_enhance_affix_state *affixes,
    vm_net_mock_equipment_bonus *bonus)
{
    vm_net_mock_equipment_enhance_attr attrs[5];
    u8 attrCount = 0;
    u8 primaryType = 0;
    u32 primaryBase = 0;
    u32 primary = 0;

    if (equipment == NULL || bonus == NULL)
        return;
    if (vm_net_mock_equipment_enhancement_resolve_primary(
            equipment, &primaryType, &primaryBase))
    {
        primary = vm_net_mock_equipment_enhancement_bonus_from_base(
            primaryBase, enhanceLevel);
        vm_net_mock_equipment_enhancement_add_attr_bonus(
            bonus, primaryType, primary);
    }
    attrCount = vm_net_mock_equipment_enhancement_collect_attrs(
        equipment, enhanceLevel, affixes, attrs,
        (u8)(sizeof(attrs) / sizeof(attrs[0])));
    for (u8 i = 0; i < attrCount; ++i)
        vm_net_mock_equipment_enhancement_add_attr_bonus(
            bonus, attrs[i].type, attrs[i].value);
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

/* `item.dsh` column 10 (堆叠数) is the authoritative maximum quantity of
 * one ordinary backpack instance.  A zero return means this item was not
 * found in the client resource catalogue; callers must preserve the legacy
 * row rather than invent a stack limit for an unresolved resource. */
static u32 vm_net_mock_item_effect_stack_limit(u32 itemId)
{
    const vm_net_mock_item_effect_catalog_item *item =
        vm_net_mock_find_item_effect_catalog_item(itemId);

    if (item == NULL || item->stack == 0)
        return 0;
    return item->stack;
}

/* These ids have client-side request/response handlers that are distinct from
 * the ordinary 7/1 consumable flow.  Keeping the classification here prevents
 * a future generic caller from silently deleting a special item just because
 * it happens to be in category 10. */
static bool vm_net_mock_item_requires_special_use_protocol(u32 itemId)
{
    switch (itemId)
    {
    case 809:
    case 810:
    case 811:
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
    u32 capacity = vm_net_mock_backpack_client_capacity(
        vm_net_mock_env_u8("CBE_ACTOR_BACKPACK_CAPACITY",
                           role ? role->backpackCapacity :
                           VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY));

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
        return vm_net_mock_shop_item_is_secret_treasure(item);
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
                                                   u32 itemId,
                                                   u8 enhanceLevel,
                                                   u8 enhanceMaxLevel,
                                                   const vm_net_mock_equipment_enhance_affix_state *affixes)
{
    /*
     * JianghuOL.CBE:ParseEquipAttributes (vtable +2452, 0x010185C2) reads
     * current enhancement then maximum enhancement as i16 values, followed
     * by u8 attr-count.  mmGame's backpack renderer reads the first value
     * from item+286 for the visible "+N" suffix.  It only consumes attr
     * slots when attr-count is nonzero, so zero-attr rows stop here.
     */
    const vm_net_mock_equipment_catalog_item *equipment =
        vm_net_mock_find_equipment_catalog_item(itemId);
    vm_net_mock_equipment_enhance_attr attrs[5];
    u8 attrCount = 0;

    if (equipment != NULL && affixes != NULL &&
        attrCount < (u8)(sizeof(attrs) / sizeof(attrs[0])))
    {
        attrCount += vm_net_mock_equipment_enhancement_collect_wire_attrs(
            equipment, (u8)SDL_min(enhanceLevel, enhanceMaxLevel), affixes,
            &attrs[attrCount],
            (u8)(sizeof(attrs) / sizeof(attrs[0]) - attrCount));
    }

    if (!vm_net_mock_seq_put_i16(out, outCap, pos, enhanceLevel))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, pos, enhanceMaxLevel))
        return false;
    if (!vm_net_mock_seq_put_u8(out, outCap, pos, attrCount))
        return false;
    for (u8 i = 0; i < attrCount; ++i)
    {
        /* ParseEquipAttributes(0x010185C2): threshold, stat type, mode,
         * signed 16-bit value.  mode=0 is the confirmed fixed-value path;
         * it avoids inventing any percentage semantics for stage bonuses. */
        if (!vm_net_mock_seq_put_u8(out, outCap, pos, attrs[i].threshold) ||
            !vm_net_mock_seq_put_u8(out, outCap, pos, attrs[i].type) ||
            !vm_net_mock_seq_put_u8(out, outCap, pos, attrs[i].mode) ||
            !vm_net_mock_seq_put_i16(out, outCap, pos, attrs[i].value))
        {
            return false;
        }
    }
    return true;
}

static bool vm_net_mock_seq_put_item_compact_extra(u8 *out, u32 outCap,
                                                    u32 *pos,
                                                    u8 enhanceLevel,
                                                    u8 enhanceMaxLevel)
{
    /*
     * 30/21 and the 17/1 row used during scene startup are item-instance
     * seeds, delivered inside larger bootstrap responses.  Their
     * ParseEquipAttributes(0x010185C2) reader defines zero stage attributes
     * as a valid compact row after current/max enhancement.
     *
     * Full +4/+8/+12/+16 metadata must not be repeated for every backpack
     * equipment row in either bootstrap packet.  The detailed 17/1 backpack
     * response and the 7/7 equipped response retain
     * vm_net_mock_seq_put_item_common_extra(), so the client still receives
     * and consumes the complete stage data where it is needed.
     */
    return vm_net_mock_seq_put_i16(out, outCap, pos, enhanceLevel) &&
           vm_net_mock_seq_put_i16(out, outCap, pos, enhanceMaxLevel) &&
           vm_net_mock_seq_put_u8(out, outCap, pos, 0);
}

static bool vm_net_mock_seq_put_shop_page_item_extra(u8 *out, u32 outCap,
                                                      u32 *pos, u32 itemId)
{
    /*
     * mmShopMstarWqvga.cbm:sub_7BC calls a shop-page item-extra reader after
     * itemId/name/visual/stack/price/stock/flag. The reader is the same
     * ParseEquipAttributes helper as mmGame:0x418C; the six attr arrays are
     * destination capacity, not fields to send when attr-count is zero.
     */
    return vm_net_mock_seq_put_item_common_extra(
        out, outCap, pos, itemId, 0,
        vm_net_mock_item_common_extra_enhance_cap(itemId), NULL);
}

static bool vm_net_mock_build_backpack_iteminfo_blob_with_stage_attrs(
    u8 *out, u32 outCap, const vm_net_mock_role_state *role,
    bool includeStageAttrs, u32 *blobLenOut, u32 *rowCountOut)
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
        if (includeStageAttrs
                ? !vm_net_mock_seq_put_item_common_extra(
                      out, outCap, &pos, item->itemId,
                      (u8)SDL_min(item->enhanceLevel,
                                  VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL),
                      vm_net_mock_item_common_extra_enhance_cap(item->itemId),
                      &item->enhanceAffixes)
                : !vm_net_mock_seq_put_item_compact_extra(
                      out, outCap, &pos,
                      (u8)SDL_min(item->enhanceLevel,
                                  VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL),
                      vm_net_mock_item_common_extra_enhance_cap(item->itemId)))
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
            if (!vm_net_mock_seq_put_item_common_extra(
                    out, outCap, &pos, item->itemId, 0,
                    vm_net_mock_item_common_extra_enhance_cap(item->itemId),
                    NULL))
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
        /* 30/21 is the bootstrap grid seed consumed by
         * HandleItemGridResponse(0x01039952).  Its row parser accepts the
         * compact current/max-enhance + zero-attribute form; the full
         * persisted +4/+8/+12/+16 plan belongs to the later detailed 17/1
         * backpack response and 7/7 equipped-item response.  Sending the
         * expanded form here overflows the client's fixed downlink parse
         * pool for large inventories and surfaces as the generic unpack
         * error before business dispatch. */
        if (!vm_net_mock_seq_put_item_compact_extra(
                out, outCap, &pos,
                (u8)SDL_min(item->enhanceLevel,
                            VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL),
                vm_net_mock_item_common_extra_enhance_cap(item->itemId)))
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
        if (!vm_net_mock_seq_put_shop_page_item_extra(out, outCap, &pos,
                                                       item->itemId))
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
    bool haveCount;
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

/* SendEquipSequenceReq (0x0101DD1E) writes `equipseq` as an ordinary object
 * entry whose value is `00 02 <u16>`.  The entry itself is length-prefixed as
 * `00 04`; the older scanner mistakes that outer length for an i32 tag and
 * reads sequence 26 as 0x0002001A.  Keep this exact accessor local to the
 * enhancement request contract. */
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

static bool vm_net_mock_build_backpack_iteminfo_blob(
    u8 *out, u32 outCap, const vm_net_mock_role_state *role,
    u32 *blobLenOut, u32 *rowCountOut)
{
    return vm_net_mock_build_backpack_iteminfo_blob_with_stage_attrs(
        out, outCap, role, true, blobLenOut, rowCountOut);
}

static bool vm_net_mock_item_id_is_active_backpack_row(u32 itemId)
{
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    return vm_net_mock_role_find_backpack_item(role, itemId, 0) != NULL;
}

static bool vm_net_mock_parse_item_use_request(
    const u8 *request, u32 requestLen, vm_net_mock_item_use_request *parsedOut)
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
    if (object.major != 1 || object.kind != 7 || object.subtype != 1 ||
        object.payloadLen == 0)
        return false;

    (void)vm_net_mock_get_object_u8_field(object.payload, object.payloadLen, "type", &parsed.type);
    if (!vm_net_mock_get_object_u16_field(object.payload, object.payloadLen,
                                          "seq", &parsed.seq))
    {
        if (vm_net_mock_get_object_number_field(object.payload, object.payloadLen,
                                                "seq", &value) &&
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
        parsed.haveCount = true;
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
    const vm_net_mock_equipment_enhance_affix_state *affixes = NULL;
    u8 enhanceLevel = 0;

    if (blobLenOut)
        *blobLenOut = 0;
    if (out == NULL || blobLenOut == NULL || itemId == 0)
        return false;
    /* In mmGame:sub_D04 only 7/7 type=1 parses iteminfo into a new item row;
     * type=2 is a current-item operation and ignores this blob.  Resolve the
     * persisted instance by sequence so a newly acquired sword carries its
     * complete future-stage plan from its first client-side lifetime.
     * Ordinary consumables keep their compact zero-extra representation. */
    if (itemId >= 1000)
    {
        const vm_net_mock_role_state *role = vm_net_mock_active_role();
        u8 itemCount = vm_net_mock_role_backpack_count(role);

        for (u8 i = 0; role != NULL && i < itemCount; ++i)
        {
            const vm_net_mock_backpack_item_state *item = &role->backpackItems[i];

            if (item->seq == seq && item->itemId == itemId)
            {
                enhanceLevel = (u8)SDL_min(
                    item->enhanceLevel, VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL);
                affixes = &item->enhanceAffixes;
                break;
            }
        }
    }
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, 1))
        return false;
    if (!vm_net_mock_seq_put_i16(out, outCap, &pos, seq))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, itemId))
        return false;
    if (!vm_net_mock_seq_put_u32(out, outCap, &pos, count))
        return false;
    if (!vm_net_mock_seq_put_item_common_extra(
            out, outCap, &pos, itemId, enhanceLevel,
            vm_net_mock_item_common_extra_enhance_cap(itemId), affixes))
        return false;

    *blobLenOut = pos;
    return true;
}

typedef struct
{
    u32 itemId;
    u16 seq;
    u32 count;
} vm_net_mock_battle_drop_result;

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
        if (rows[i].seq == 0 || rows[i].itemId == 0 || rows[i].count == 0 ||
            !vm_net_mock_seq_put_i16(out, outCap, &pos, rows[i].seq) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, rows[i].itemId) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, rows[i].count) ||
            !vm_net_mock_seq_put_item_common_extra(
                out, outCap, &pos, rows[i].itemId, 0,
                vm_net_mock_item_common_extra_enhance_cap(rows[i].itemId),
                NULL))
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
    u8 *out, u32 outCap, u32 *pos, u16 seq, u32 itemId, u32 count)
{
    u8 itemInfo[VM_NET_MOCK_ITEM_USE_ITEMINFO_MAX_BYTES];
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
        u32 itemId = role->equippedItems[slot].itemId;

        if (itemId != 0 &&
            vm_net_mock_role_equipment_slot_is_usable(role, slot))
            ++rowCount;
    }
    if (!vm_net_mock_seq_put_u8(out, outCap, &pos, rowCount))
        return false;

    for (u8 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
    {
        const vm_net_mock_equipped_item_state *item = &role->equippedItems[slot];
        u32 itemId = item->itemId;
        u32 durability = item->durability;

        /* mmGameMstarWqvga.cbm:sub_D04 reads every 7/7 row as
         * seq(u16), itemId(u32), current-count(u32), and the common equipment
         * attributes.  For item ids >= 1000 it writes current-count to the
         * equipment current-durability field at item+272. */
        if (itemId == 0 || item->durabilityMax == 0 ||
            !vm_net_mock_role_equipment_slot_is_usable(role, slot))
            continue;
        if (!vm_net_mock_seq_put_i16(out, outCap, &pos, (u16)(slot + 1)) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, itemId) ||
            !vm_net_mock_seq_put_u32(out, outCap, &pos, durability) ||
            !vm_net_mock_seq_put_item_common_extra(
                out, outCap, &pos,
                itemId,
                (u8)SDL_min(item->enhanceLevel,
                            VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL),
                VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL,
                &item->enhanceAffixes))
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
        /* Special-item clients emit ordinary length-prefixed field entries.
         * A u16 sequence therefore has the exact value bytes
         * `00 02 <seq>`, wrapped by entry-length `00 04`.  The historical
         * loose u32 scanner treats that outer length as a value tag and
         * turns sequence 17 into 0x00020011; it then rejects the packet as
         * out of the client u16 sequence range.  Decode the entry grammar
         * itself, as the chest and enhancement sequence contracts do. */
        !vm_net_mock_get_object_tagged_number_entry(
            object.payload, object.payloadLen, seqField, &sequence) ||
        sequence == 0 || sequence > 0xffffu)
    {
        return false;
    }
    if (requireOneNum &&
        (!vm_net_mock_get_object_tagged_number_entry(
             object.payload, object.payloadLen, "num", &num) ||
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
    default:
        return 0;
    }
}

/* Every time-limited category-21 item is submitted through the same 1/22/3
 * client request, but item.dsh gives them three distinct effect families.
 * Keep the mapping declarative and verify its duration/category against the
 * loaded DSH row before consuming an item.  The event candy series contains
 * two copies of each recipe (old event IDs 525..531 and store IDs 820..826).
 */
typedef struct
{
    u32 itemId;
    u8 effectKind;
    u8 multiplier;
    u8 durationMinutes;
} vm_net_mock_timed_combat_item_spec;

static const vm_net_mock_timed_combat_item_spec g_vm_net_mock_timed_combat_item_specs[] = {
    {525u, VM_NET_MOCK_ROLE_ITEM_EFFECT_EVENT_ATTACK, 10u, 15u},
    {526u, VM_NET_MOCK_ROLE_ITEM_EFFECT_EVENT_ATTACK, 40u, 30u},
    {527u, VM_NET_MOCK_ROLE_ITEM_EFFECT_EVENT_ATTACK, 100u, 60u},
    {528u, VM_NET_MOCK_ROLE_ITEM_EFFECT_EVENT_DEFENSE, 10u, 15u},
    {529u, VM_NET_MOCK_ROLE_ITEM_EFFECT_EVENT_DEFENSE, 40u, 30u},
    {530u, VM_NET_MOCK_ROLE_ITEM_EFFECT_EVENT_DEFENSE, 100u, 60u},
    {531u, VM_NET_MOCK_ROLE_ITEM_EFFECT_EVENT_ATTACK_DEFENSE, 120u, 60u},
    {820u, VM_NET_MOCK_ROLE_ITEM_EFFECT_EVENT_ATTACK, 10u, 15u},
    {821u, VM_NET_MOCK_ROLE_ITEM_EFFECT_EVENT_ATTACK, 40u, 30u},
    {822u, VM_NET_MOCK_ROLE_ITEM_EFFECT_EVENT_ATTACK, 100u, 60u},
    {823u, VM_NET_MOCK_ROLE_ITEM_EFFECT_EVENT_DEFENSE, 10u, 15u},
    {824u, VM_NET_MOCK_ROLE_ITEM_EFFECT_EVENT_DEFENSE, 40u, 30u},
    {825u, VM_NET_MOCK_ROLE_ITEM_EFFECT_EVENT_DEFENSE, 100u, 60u},
    {826u, VM_NET_MOCK_ROLE_ITEM_EFFECT_EVENT_ATTACK_DEFENSE, 120u, 60u},
    /* The original item rows describe these as “明显/巨幅提升” without an
     * exact numeric column.  These 50%%/100%% values are an explicit server
     * balance policy, stored with the effect and applied identically to attack
     * and defence; they are not claimed to be hidden DSH values. */
    {829u, VM_NET_MOCK_ROLE_ITEM_EFFECT_COMBAT_PILL, 50u, 30u},
    {830u, VM_NET_MOCK_ROLE_ITEM_EFFECT_COMBAT_PILL, 100u, 30u}
};

static const vm_net_mock_timed_combat_item_spec *
vm_net_mock_find_timed_combat_item_spec(u32 itemId)
{
    for (u32 i = 0;
         i < sizeof(g_vm_net_mock_timed_combat_item_specs) /
                 sizeof(g_vm_net_mock_timed_combat_item_specs[0]);
         ++i)
    {
        if (g_vm_net_mock_timed_combat_item_specs[i].itemId == itemId)
            return &g_vm_net_mock_timed_combat_item_specs[i];
    }
    return NULL;
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
    case 829:
        return "\xCA\xA7\xB4\xAB\xD2\xD1\xBE\xC3\xB5\xC4\xC9\xF1\xC3\xD8\xB5\xA4\xD2\xA9\xA3\xAC\xB7\xFE\xD3\xC3\xBA\xF3\x33\x30\xB7\xD6\xD6\xD3\xC4\xDA\xC9\xCB\xBA\xA6\xBA\xCD\xB7\xC0\xD3\xF9\xD0\xA7\xB9\xFB\xC3\xF7\xCF\xD4\xCC\xE1\xC9\xFD\xA1\xA3";
    case 830:
        return "\xCA\xA7\xB4\xAB\xD2\xD1\xBE\xC3\xB5\xC4\xC9\xF1\xC3\xD8\xB5\xA4\xD2\xA9\xA3\xAC\xB7\xFE\xD3\xC3\xBA\xF3\x33\x30\xB7\xD6\xD6\xD3\xC4\xDA\xC9\xCB\xBA\xA6\xBA\xCD\xB7\xC0\xD3\xF9\xD0\xA7\xB9\xFB\xBE\xDE\xB7\xF9\xCC\xE1\xC9\xFD\xA3\xAC\xBC\xF2\xD6\xB1\xCA\xC7\xC8\xCB\xB5\xB2\xC9\xB1\xC8\xC8\xCB\xB7\xF0\xB5\xB2\xC9\xB1\xB7\xF0\xB0\xA1\xA3";
    case 525:
    case 820:
        return "\xB9\xA5\xBB\xF7\xC1\xA6\xCC\xE1\xC9\xFD\x31\x30\x25\xA3\xAC\xB3\xD6\xD0\xF8\x31\x35\xB7\xD6\xD6\xD3\xA1\xA3";
    case 526:
    case 821:
        return "\xB9\xA5\xBB\xF7\xC1\xA6\xCC\xE1\xC9\xFD\x34\x30\x25\xA3\xAC\xB3\xD6\xD0\xF8\x33\x30\xB7\xD6\xD6\xD3\xA1\xA3";
    case 527:
    case 822:
        return "\xB9\xA5\xBB\xF7\xC1\xA6\xCC\xE1\xC9\xFD\x31\x30\x30\x25\xA3\xAC\xB3\xD6\xD0\xF8\x36\x30\xB7\xD6\xD6\xD3\xA1\xA3";
    case 528:
    case 823:
        return "\xB7\xC0\xD3\xF9\xC1\xA6\xCC\xE1\xC9\xFD\x31\x30\x25\xA3\xAC\xB3\xD6\xD0\xF8\x31\x35\xB7\xD6\xD6\xD3\xA1\xA3";
    case 529:
    case 824:
        return "\xB7\xC0\xD3\xF9\xC1\xA6\xCC\xE1\xC9\xFD\x34\x30\x25\xA3\xAC\xB3\xD6\xD0\xF8\x33\x30\xB7\xD6\xD6\xD3\xA1\xA3";
    case 530:
    case 825:
        return "\xB7\xC0\xD3\xF9\xC1\xA6\xCC\xE1\xC9\xFD\x31\x30\x30\x25\xA3\xAC\xB3\xD6\xD0\xF8\x36\x30\xB7\xD6\xD6\xD3\xA1\xA3";
    case 531:
    case 826:
        return "\xB9\xA5\xBB\xF7\xC1\xA6\xD3\xEB\xB7\xC0\xD3\xF9\xC1\xA6\xCC\xE1\xC9\xFD\x31\x32\x30\x25\xA3\xAC\xB3\xD6\xD0\xF8\x36\x30\xB7\xD6\xD6\xD3\xA1\xA3";
    default:
        return "OK";
    }
}

/* JianghuOL.CBE:0x01011A5E consumes `expinfo` from 1/7/31.  A non-empty
 * value also marks the client-side exp-card status as fresh; an empty value
 * is paired with 1/7/32 below to clear a card which expired while the player
 * remained online. */
static const char *vm_net_mock_exp_card_active_info(u32 multiplier)
{
    switch (multiplier)
    {
    case 2:
        return "\xCB\xAB\xB1\xB6\xBE\xAD\xD1\xE9\xBF\xA8\xC9\xFA\xD0\xA7\xD6\xD0";
    case 4:
        return "\xCB\xC4\xB1\xB6\xBE\xAD\xD1\xE9\xBF\xA8\xC9\xFA\xD0\xA7\xD6\xD0";
    case 10:
        return "\xCA\xAE\xB1\xB6\xBE\xAD\xD1\xE9\xBF\xA8\xC9\xFA\xD0\xA7\xD6\xD0";
    default:
        return "";
    }
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
 * experience card.  The response shape is taken directly from
 * net_handle_misc_player_fields and HandleExpInfoResponse: `expinfo` is
 * always present and `expcard` is sent only for the expired branch. */
static u32 vm_net_mock_build_exp_card_status_response(const u8 *request,
                                                      u32 requestLen,
                                                      u8 *out, u32 outCap)
{
    u32 multiplier = 1;
    u32 pos = 5;
    u32 objectStart = 0;

    if (!vm_net_mock_is_exp_card_status_request(request, requestLen) ||
        out == NULL || outCap < pos)
    {
        return 0;
    }

    multiplier = vm_net_mock_role_active_exp_card_multiplier(vm_net_mock_active_role());
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 31, &objectStart) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "expinfo",
                                       vm_net_mock_exp_card_active_info(multiplier)))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    if (multiplier <= 1)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 32, &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "expcard", 0))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
    }
    vm_net_mock_finish_wt_packet(out, pos, multiplier > 1 ? 1 : 2);

    printf("[info][network] mock_exp_card_status multiplier=%u active=%u response=%u evidence=JianghuOL.CBE:0x0100E3B8+0x01011A5E\n",
           multiplier, multiplier > 1 ? 1u : 0u, pos);
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
    const vm_net_mock_timed_combat_item_spec *combatSpec = NULL;
    vm_net_mock_role_item_effect effect;
    u32 resolvedItemId = 0;
    u32 multiplier = 0;
    u32 durationSeconds = 0;
    u32 now = (u32)time(NULL);
    bool isExpCard = false;
    bool isBattleInsight = false;
    bool isTimedCombatItem = false;
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
        isTimedCombatItem = true;
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
        else if (isTimedCombatItem)
        {
            combatSpec = vm_net_mock_find_timed_combat_item_spec(item->itemId);
            if (combatSpec != NULL && catalogItem != NULL &&
                catalogItem->category == 21 &&
                catalogItem->consumeMode == 1 &&
                catalogItem->durationMinutes == combatSpec->durationMinutes)
            {
                effect.kind = combatSpec->effectKind;
                effect.itemId = item->itemId;
                effect.multiplier = combatSpec->multiplier;
                durationSeconds = (u32)catalogItem->durationMinutes * 60u;
            }
        }
        if (durationSeconds != 0 && now <= 0xffffffffu - durationSeconds)
        {
            effect.expiresUnix = now + durationSeconds;
            success = vm_net_mock_role_consume_backpack_item_with_timed_effect(
                role, item->itemId, requestedSeq, &effect, durationSeconds, NULL,
                isExpCard ? "exp-card-use"
                          : (isBattleInsight ? "battle-insight-use"
                                             : "timed-combat-item-use"));
            if (success)
                info = isBattleInsight
                           ? "\xD5\xBD\xB6\xB7\xD0\xC4\xB5\xC3\xD0\xA7\xB9\xFB\xD2\xD1\xC9\xFA\xD0\xA7\xA3\xAC\xBE\xAD\xD1\xE9\xD4\xF6\xBC\xD3\x32\x30\x25\xA1\xA3"
                           : vm_net_mock_special_item_success_info(resolvedItemId);
            else
                info = "\xCD\xAC\xC0\xE0\xD0\xA7\xB9\xFB\xD2\xD1\xC9\xFA\xD0\xA7\xA3\xAC\xC7\xEB\xB5\xC8\xB4\xFD\xBD\xE1\xCA\xF8\xBA\xF3\xD4\xD9\xCA\xB9\xD3\xC3\xA1\xA3";
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
    }
    else if (requestKind == 25 && requestSubtype == 6)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 25, 6, &objectStart) ||
            !vm_net_mock_put_object_u8(out, outCap, &pos, "result", success ? 1 : 2) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "maxnum", 0) ||
            !vm_net_mock_put_object_string(out, outCap, &pos, "iteminfo", info))
        {
            return 0;
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
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);

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

/* 827 修炼丹 is now backed by the same account/role lifecycle as the
 * practise panel.  Keep it ahead of the historical unresolved-special
 * fallback: result=1 is meaningful to the client (it removes the selected
 * stack row, closes the progress dialog and requests a fresh backpack grid). */
static u32 vm_net_mock_build_practise_pill16_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = NULL;
    u16 itemSeq = 0;
    u32 practiseMinutes = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    bool success = false;
    const char *itemInfo =
        "\xD0\xDE\xC1\xB6\xCA\xB1\xBC\xE4\xD4\xF6\xBC\xD3\x31\xD0\xA1\xCA\xB1\xA1\xA3"; /* 修炼时间增加1小时。 */

    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 16,
                                                    "itemseq", false, &itemSeq))
    {
        return 0;
    }
    role = vm_net_mock_active_role();
    success = vm_net_mock_practise_use_pill(role, itemSeq, &practiseMinutes);
    if (!success)
    {
        itemInfo =
            "\xD0\xDE\xC1\xB6\xCA\xB1\xBC\xE4\xD2\xD1\xB4\xEF\xB5\xBD\xC0\xDB\xBC\xC6\xC9\xCF\xCF\xDE\xA3\xAC\xCE\xB4\xCA\xB9\xD3\xC3\xA1\xA3"; /* 修炼时间已达到累计上限，未使用。 */
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 16, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", success ? 1 : 2) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "maxnum", practiseMinutes) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "iteminfo", itemInfo))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_practise_pill16 role=%u seq=%u success=%u practise_minutes=%u response=%u evidence=item.dsh:827+JianghuOL.CBE:0x0102355E+0x0102615A\n",
           role ? role->roleId : 0, itemSeq, success ? 1u : 0u,
           practiseMinutes, pos);
    return pos;
}

/* 833 聚元丹 uses the same CBE result shell as 827, but its `maxnum` is the
 * current vitality, not a disguised HP/MP amount.  result=1 is emitted only
 * after the exact selected stack and account_role_vitality row committed in
 * one transaction.  HandleShopBuyItem's 7/33 branch does not write the
 * role's energy cache, so a successful operation is followed by the native
 * 2/13 energy update that net_handle_actor_move_info already consumes. */
static u32 vm_net_mock_build_vitality_pill33_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = NULL;
    u16 itemSeq = 0;
    u32 current = 0;
    u32 maximum = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 responseObjectCount = 1;
    bool success = false;
    const char *itemInfo =
        "\xBB\xEE\xC1\xA6\xD2\xD1\xC2\xFA\xA3\xAC\xCE\xB4\xCA\xB9\xD3\xC3\xA1\xA3"; /* 活力已满，未使用。 */

    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 33,
                                                    "itemseq", false, &itemSeq))
    {
        return 0;
    }
    role = vm_net_mock_active_role();
    success = vm_net_mock_vitality_use_pill(role, itemSeq, &current, &maximum);
    if (success)
    {
        itemInfo = "\xBB\xEE\xC1\xA6\xBB\xD6\xB8\xB4\xB3\xC9\xB9\xA6\xA1\xA3"; /* 活力恢复成功。 */
    }
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 33, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", success ? 1 : 2) ||
        !vm_net_mock_put_object_u32(out, outCap, &pos, "maxnum", current) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "iteminfo", itemInfo))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    if (success)
    {
        if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 2, 13,
                                         &objectStart) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "energy", current) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "energymax", maximum))
        {
            return 0;
        }
        vm_net_mock_finish_wt_object(out, objectStart, pos);
        responseObjectCount = 2;
    }
    vm_net_mock_finish_wt_packet(out, pos, responseObjectCount);
    printf("[info][network] mock_vitality_pill33 role=%u seq=%u success=%u vitality=%u/%u response_objects=%u response=%u evidence=JianghuOL.CBE:0x0102355E+0x01025AE6+0x01012ADC(case13),item.dsh:833\n",
           role ? role->roleId : 0, itemSeq, success ? 1u : 0u,
           current, maximum, responseObjectCount, pos);
    return pos;
}

/* 921 is the actual sequence-owned transfer item.  CBE case 40 consumes the
 * currently selected row only for result=0 and then calls HandleLevelUpResponse,
 * so all status fields must be from the same committed role snapshot. */
static u32 vm_net_mock_build_training_book_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_role_state before;
    vm_net_mock_training_book_record book;
    u16 itemSeq = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u32 currentLevel = 0;
    u32 beforeExp = 0;
    u32 remaining = 0;
    bool success = false;
    const char *bookInfo =
        "\xB1\xB3\xB0\xFC\xD6\xD0\xB5\xC4\xCC\xEC\xCA\xE9\xB2\xBB\xB4\xE6\xD4\xDA\xA3\xAC\xCE\xB4\xCA\xB9\xD3\xC3\xA1\xA3"; /* 背包中的天书不存在，未使用。 */

    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 40,
                                                    "seq", false, &itemSeq))
    {
        return 0;
    }
    role = vm_net_mock_active_role();
    memset(&book, 0, sizeof(book));
    if (role != NULL &&
        vm_net_mock_training_book_load_active_instance(role, itemSeq, &book))
    {
        currentLevel = vm_net_mock_role_level_from_exp(role->exp);
        if (book.level == 0 || book.level > VM_NET_MOCK_ROLE_LEVEL_CAP ||
            book.experience == 0 ||
            book.experience < vm_net_mock_role_level_start_exp(book.level))
        {
            bookInfo = "\xCC\xEC\xCA\xE9\xBE\xAD\xD1\xE9\xCE\xDE\xD0\xA7\xA3\xAC\xCE\xB4\xCA\xB9\xD3\xC3\xA1\xA3"; /* 天书经验无效，未使用。 */
        }
        else if (currentLevel >= VM_NET_MOCK_ROLE_LEVEL_CAP ||
                 book.level <= currentLevel)
        {
            bookInfo = "\xCC\xEC\xCA\xE9\xB5\xC8\xBC\xB6\xB1\xD8\xD0\xEB\xB8\xDF\xD3\xDA\xB5\xB1\xC7\xB0\xB5\xC8\xBC\xB6\xA1\xA3"; /* 天书等级必须高于当前等级。 */
        }
        else
        {
            before = *role;
            beforeExp = role->exp;
            if (vm_net_mock_role_consume_backpack_item(role, 921, itemSeq, 1,
                                                        &remaining))
            {
                (void)vm_net_mock_role_add_exp(role, book.experience);
                if (role->exp > beforeExp &&
                    vm_net_mock_role_db_save("training-book-use"))
                {
                    success = true;
                    bookInfo = "\xD0\xDE\xC1\xB6\xCC\xEC\xCA\xE9\xCA\xB9\xD3\xC3\xB3\xC9\xB9\xA6\xA1\xA3"; /* 修炼天书使用成功。 */
                }
                else
                {
                    *role = before;
                    bookInfo = "\xD0\xDE\xC1\xB6\xCC\xEC\xCA\xE9\xCA\xB9\xD3\xC3\xCA\xA7\xB0\xDC\xA3\xAC\xCE\xB4\xCA\xB9\xD3\xC3\xA1\xA3"; /* 修炼天书使用失败，未使用。 */
                }
            }
        }
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 40, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", success ? 0 : 1) ||
        !vm_net_mock_put_object_string(out, outCap, &pos, "bookinfo", bookInfo))
    {
        return 0;
    }
    if (success)
    {
        if (!vm_net_mock_put_object_u32(out, outCap, &pos, "exp", role->exp) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "level", role->level) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "lastexp",
                                        vm_net_mock_role_last_level_exp(role->exp)) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "curexp",
                                        vm_net_mock_role_next_level_start_exp(role->exp)) ||
            !vm_net_mock_put_object_u32(out, outCap, &pos, "persentexp",
                                        vm_net_mock_role_exp_percent(role->exp)))
        {
            return 0;
        }
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    printf("[info][network] mock_training_book_use role=%u seq=%u book_level=%u book_exp=%u recipient_level=%u success=%u item_remaining=%u response=%u evidence=JianghuOL.CBE:0x010238B6+0x01025AE6(case40)+0x01046EDA\n",
           role ? role->roleId : 0, itemSeq, book.level, book.experience,
           currentLevel, success ? 1u : 0u, remaining, pos);
    return pos;
}

/* Description-only book reads stay separate from the consuming 7/40 transfer
 * transaction.  7/35 is the static template text; 7/38 reads a concrete 921
 * instance.  The use packets 7/16, 7/33 and 7/40 are handled by their own
 * durable builders above and must never fall back here. */
static u32 vm_net_mock_build_unresolved_special_item_response(
    const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u16 requestedSeq = 0;
    u8 subtype = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    const char *bookInfo =
        "\xD0\xDE\xC1\xB6\xCC\xEC\xCA\xE9\xD7\xCA\xC1\xCF\xC9\xD0\xCE\xB4\xC5\xE4\xD6\xC3\xA3\xAC\xCE\xB4\xCF\xFB\xBA\xC4\xA1\xA3";
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_training_book_record trainingBook;
    bool trainingBookLoaded = false;

    if (out == NULL || outCap < pos)
        return 0;
    if (vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 35,
                                                        "seq", false, &requestedSeq))
    {
        subtype = 35;
    }
    else if (vm_net_mock_parse_special_item_seq_request(request, requestLen, 7, 38,
                                                        "seq", false, &requestedSeq))
    {
        subtype = 38;
    }
    else
    {
        return 0;
    }

    /* 921 is a non-stackable, sequence-owned instance.  Unlike 920 its DSH
     * description is intentionally empty, so return only its persisted
     * per-instance fields through the client-proven 7/38 and 7/40 contracts. */
    if (subtype == 38 || subtype == 40)
    {
        role = vm_net_mock_active_role();
        memset(&trainingBook, 0, sizeof(trainingBook));
        if (vm_net_mock_training_book_load_active_instance(role, requestedSeq,
                                                           &trainingBook))
        {
            bookInfo = subtype == 38 ? trainingBook.description : trainingBook.bookInfo;
            trainingBookLoaded = true;
        }
    }
    else if (subtype == 35)
    {
        /* Static 920 retains its ordinary, resource-provided book text and
         * remains non-consuming until the experience-transfer operation has
         * its own authoritative gameplay implementation. */
        bookInfo = g_vm_net_mock_training_book_default_description;
    }

    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, subtype, &objectStart))
        return 0;
    if (subtype == 35)
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
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    vm_net_mock_finish_wt_packet(out, pos, 1);
    if (trainingBookLoaded)
    {
        printf("[info][network] mock_training_book_instance_read request=7/%u seq=%u role=%u action=not-consumed response=%u evidence=JianghuOL.CBE:0x010238B6+0x01025AE6\n",
               subtype, requestedSeq, role ? role->roleId : 0, pos);
    }
    else
    {
        printf("[warn][network] mock_special_item_unresolved request=7/%u seq=%u action=not-consumed response=%u evidence=JianghuOL.CBE:0x01025AE6\n",
               subtype, requestedSeq, pos);
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

typedef struct
{
    u32 chestItemId;
    u16 chestSeq;
    u16 keySeq;
    u32 count;
    u8 itemUseType;
} vm_net_mock_chest_open_request;

/*
 * Native chest-open packet captured from guest00024:
 *
 *   WT 7/15 { box: tagged-u16(backpack sequence),
 *             key: tagged-u16(backpack sequence) }
 *
 * `box` and `key` are not item IDs.  They identify the exact instances that
 * the client selected, so the server must validate both rows before it draws
 * a reward.  Keep this decoder local to chest opening; unknown 7/15 packets
 * remain unhandled instead of becoming generic consumable requests.
 */
static bool vm_net_mock_parse_chest_open_request(
    const u8 *request, u32 requestLen,
    vm_net_mock_chest_open_request *parsedOut,
    u8 *requestSubtypeOut)
{
    vm_net_mock_chest_open_request parsed;
    vm_net_mock_item_use_request itemUse;
    vm_net_mock_request_object object;
    u32 offset = 4;
    u32 boxSeq = 0;
    u32 keySeq = 0;

    memset(&parsed, 0, sizeof(parsed));
    memset(&itemUse, 0, sizeof(itemUse));
    if (parsedOut)
        memset(parsedOut, 0, sizeof(*parsedOut));
    if (requestSubtypeOut)
        *requestSubtypeOut = 0;

    if (request != NULL && requestLen >= 9 && request[0] == 'W' &&
        request[1] == 'T' &&
        vm_net_mock_next_request_object(request, requestLen, &offset,
                                        &object) &&
        offset == requestLen && object.major == 1 && object.kind == 7 &&
        object.subtype == 15 &&
        vm_net_mock_get_object_tagged_number_entry(
            object.payload, object.payloadLen, "box", &boxSeq) &&
        vm_net_mock_get_object_tagged_number_entry(
            object.payload, object.payloadLen, "key", &keySeq) &&
        boxSeq != 0 && boxSeq <= 0xffffu && keySeq != 0 &&
        keySeq <= 0xffffu)
    {
        parsed.chestSeq = (u16)boxSeq;
        parsed.keySeq = (u16)keySeq;
        parsed.count = 1;
        parsed.itemUseType = 1;
        if (parsedOut)
            *parsedOut = parsed;
        if (requestSubtypeOut)
            *requestSubtypeOut = 15;
        return true;
    }
    if (!vm_net_mock_parse_item_use_request(request, requestLen, &itemUse))
        return false;

    parsed.chestItemId = itemUse.itemId;
    parsed.chestSeq = itemUse.seq;
    parsed.count = itemUse.count;
    parsed.itemUseType = itemUse.type;
    if (parsedOut)
        *parsedOut = parsed;
    if (requestSubtypeOut)
        *requestSubtypeOut = 1;
    return true;
}

/* Resolve the selected sequence against the active backpack before taking
 * precedence over the broad `1/7/1` consumable handler. */
static bool vm_net_mock_is_chest_open_request(const u8 *request, u32 requestLen)
{
    vm_net_mock_chest_open_request parsed;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *item = NULL;

    if (!vm_net_mock_parse_chest_open_request(request, requestLen, &parsed,
                                              NULL))
        return false;
    role = vm_net_mock_active_role();
    item = vm_net_mock_role_find_backpack_item(role, parsed.chestItemId,
                                               parsed.chestSeq);
    return item != NULL && vm_net_mock_chest_kind_index(item->itemId) >= 0;
}

static u32 vm_net_mock_chest_next_random(const vm_net_mock_role_state *role)
{
    u32 seed = 0;

    if (g_vm_net_mock_chest_reward_rng == 0)
    {
        seed = 0x9e3779b9u ^ (u32)time(NULL) ^ scheduler_get_tick_ms() ^
               (++g_vm_net_mock_chest_reward_rng_serial * 0x85ebca6bu) ^
               (role ? role->roleId * 0xc2b2ae35u : 0u);
        if (seed == 0)
            seed = 0x6d2b79f5u;
        g_vm_net_mock_chest_reward_rng = seed;
    }
    g_vm_net_mock_chest_reward_rng ^= g_vm_net_mock_chest_reward_rng << 13;
    g_vm_net_mock_chest_reward_rng ^= g_vm_net_mock_chest_reward_rng >> 17;
    g_vm_net_mock_chest_reward_rng ^= g_vm_net_mock_chest_reward_rng << 5;
    return g_vm_net_mock_chest_reward_rng;
}

static const vm_net_mock_chest_reward *vm_net_mock_chest_draw_reward(
    const vm_net_mock_chest_admin_row *chest, const vm_net_mock_role_state *role,
    u32 *totalWeightOut, u32 *drawOut)
{
    u32 totalWeight = 0;
    u32 draw = 0;
    uint64_t acceptedLimit = 0;

    if (totalWeightOut)
        *totalWeightOut = 0;
    if (drawOut)
        *drawOut = 0;
    if (chest == NULL || chest->rewardCount == 0 ||
        chest->rewardCount > VM_NET_MOCK_CHEST_REWARD_MAX)
    {
        return NULL;
    }
    for (u8 i = 0; i < chest->rewardCount; ++i)
    {
        if (chest->rewards[i].itemId == 0 || chest->rewards[i].count == 0 ||
            chest->rewards[i].weight == 0 ||
            chest->rewards[i].weight >
                VM_NET_MOCK_CHEST_REWARD_WEIGHT_MAX ||
            0xffffffffu - totalWeight < chest->rewards[i].weight)
        {
            return NULL;
        }
        totalWeight += chest->rewards[i].weight;
    }
    if (totalWeight == 0)
        return NULL;

    /* Rejection sampling avoids giving the low-valued rows a tiny modulo
     * advantage when the configured total does not divide UINT32_MAX+1. */
    acceptedLimit = 0x100000000ull -
                    (0x100000000ull % (uint64_t)totalWeight);
    do
    {
        draw = vm_net_mock_chest_next_random(role);
    } while ((uint64_t)draw >= acceptedLimit);
    draw %= totalWeight;
    if (totalWeightOut)
        *totalWeightOut = totalWeight;
    if (drawOut)
        *drawOut = draw;
    for (u8 i = 0; i < chest->rewardCount; ++i)
    {
        if (draw < chest->rewards[i].weight)
            return &chest->rewards[i];
        draw -= chest->rewards[i].weight;
    }
    return NULL;
}

static const char *vm_net_mock_chest_world_broadcast_name_gbk(u32 chestItemId)
{
    /* The catalog/editor labels are UTF-8 source strings, whereas chat
     * payloads are GBK.  Keep the packet-facing identity beside the chest ids
     * instead of forwarding a UTF-8 source literal to the client. */
    switch (chestItemId)
    {
    case 522:
        return "\xC7\xE0\xCD\xAD\xB1\xA6\xCF\xE4"; /* 青铜宝箱 */
    case 523:
        return "\xB0\xD7\xD2\xF8\xB1\xA6\xCF\xE4"; /* 白银宝箱 */
    case 524:
        return "\xBB\xC6\xBD\xF0\xB1\xA6\xCF\xE4"; /* 黄金宝箱 */
    default:
        return NULL;
    }
}

/* `1/16/2.result=4` is the mmGame text-notice path.  Keep this separate from
 * the 1/7/37 item-acquire path: the latter inserts an item itself and would
 * duplicate the already-proven 1/7/7 type=1 backpack increment below. */
static bool vm_net_mock_format_chest_open_reward_hint_gbk(
    u32 chestItemId, const char *rewardNameGbk, u32 rewardCount,
    char *hintOut, size_t hintOutCap)
{
    static const char openedGbk[] = "\xBF\xAA\xC6\xF4"; /* 开启 */
    static const char receivedGbk[] = "\xA3\xAC\xBB\xF1\xB5\xC3"; /* ，获得 */
    static const char multiplierGbk[] = "\xA1\xC1"; /* × */
    const char *chestNameGbk = vm_net_mock_chest_world_broadcast_name_gbk(chestItemId);
    int written = 0;

    if (hintOut == NULL || hintOutCap == 0)
        return false;
    hintOut[0] = 0;
    if (chestNameGbk == NULL || chestNameGbk[0] == 0 ||
        rewardNameGbk == NULL || rewardNameGbk[0] == 0 || rewardCount == 0)
    {
        return false;
    }
    if (rewardCount == 1)
        written = snprintf(hintOut, hintOutCap, "%s%s%s%s", openedGbk,
                           chestNameGbk, receivedGbk, rewardNameGbk);
    else
        written = snprintf(hintOut, hintOutCap, "%s%s%s%s%s%u", openedGbk,
                           chestNameGbk, receivedGbk, rewardNameGbk,
                           multiplierGbk, rewardCount);
    if (written <= 0 || (size_t)written >= hintOutCap)
    {
        hintOut[0] = 0;
        return false;
    }
    return true;
}

static bool vm_net_mock_append_chest_open_reward_hint_object(
    u8 *out, u32 outCap, u32 *pos, u8 *objectCount, u32 chestItemId,
    const char *rewardNameGbk, u32 rewardCount)
{
    char hint[192];
    u32 objectStart = 0;

    if (out == NULL || pos == NULL || objectCount == NULL || *objectCount == 0xFF ||
        !vm_net_mock_format_chest_open_reward_hint_gbk(chestItemId, rewardNameGbk,
                                                       rewardCount, hint, sizeof(hint)) ||
        !vm_net_mock_begin_wt_object(out, outCap, pos, 1, 16, 2, &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, pos, "result", 4) ||
        !vm_net_mock_put_object_string(out, outCap, pos, "hint", hint))
    {
        return false;
    }
    vm_net_mock_finish_wt_object(out, objectStart, *pos);
    ++*objectCount;
    return true;
}

/*
 * Client contract:
 * - JianghuOL.CBE:0x01033544 consumes 1/7/1 only for the pending item-use
 *   acknowledgement.
 * - mmGame sub_11CE/sub_D04 consumes 1/7/7 type=2 as a selected-row update
 *   and type=1 as a one-shot additive reward row.
 * - the same CBE parser consumes 1/7/11 to synchronize the item count.
 *
 * 1/7/37 is intentionally absent: HandleItemAcquire can insert an item too,
 * and battle-reward runtime evidence shows pairing it with 7/7 type=1 risks a
 * duplicate local add.  The proven no-popup 7/7 path is sufficient here.
 */
static u32 vm_net_mock_build_chest_open_response(const u8 *request,
                                                 u32 requestLen,
                                                 u8 *out, u32 outCap)
{
    vm_net_mock_chest_open_request parsed;
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_backpack_item_state *chestItem = NULL;
    vm_net_mock_backpack_item_state *keyItem = NULL;
    const vm_net_mock_chest_admin_row *chest = NULL;
    const vm_net_mock_chest_reward *reward = NULL;
    const vm_net_mock_shop_catalog_item *rewardCatalogItem = NULL;
    vm_net_mock_backpack_item_state *rewardItem = NULL;
    vm_net_mock_role_state before;
    vm_net_mock_role_state projected;
    int chestIndex = -1;
    u16 rewardSeq = 0;
    u32 chestRemaining = 0;
    u32 keyRemaining = 0;
    u32 rewardWireCount = 0;
    u32 totalWeight = 0;
    u32 draw = 0;
    u32 pos = 5;
    u32 objectStart = 0;
    u8 objectCount = 0;
    u8 itemUseType = 1;
    u8 requestSubtype = 0;

    if (out == NULL || outCap < pos ||
        !vm_net_mock_parse_chest_open_request(request, requestLen, &parsed,
                                              &requestSubtype))
    {
        return 0;
    }
    role = vm_net_mock_active_role();
    chestItem = vm_net_mock_role_find_backpack_item(role, parsed.chestItemId,
                                                     parsed.chestSeq);
    if (role == NULL || chestItem == NULL ||
        (chestIndex = vm_net_mock_chest_kind_index(chestItem->itemId)) < 0)
    {
        return 0;
    }
    if (parsed.count != 1)
        return vm_net_mock_build_item_use_hint_response(
            out, outCap, "Open one chest per request");
    if (!vm_net_mock_chest_admin_db_load())
        return vm_net_mock_build_item_use_hint_response(
            out, outCap, "Chest reward pool unavailable");

    chest = &g_vm_net_mock_chest_rows[chestIndex];
    if (chest->rewardCount == 0)
        return vm_net_mock_build_item_use_hint_response(
            out, outCap, "Chest reward pool is not configured");
    keyItem = vm_net_mock_role_find_backpack_item(role,
                                                   parsed.keySeq ? 0 : chest->keyItemId,
                                                   parsed.keySeq);
    if (keyItem == NULL || keyItem->itemId != chest->keyItemId ||
        keyItem->count == 0)
        return vm_net_mock_build_item_use_hint_response(out, outCap,
                                                         "Matching key required");

    reward = vm_net_mock_chest_draw_reward(chest, role, &totalWeight, &draw);
    rewardCatalogItem = reward == NULL ? NULL :
        vm_net_mock_find_shop_catalog_item(reward->itemId);
    if (rewardCatalogItem == NULL)
        return vm_net_mock_build_item_use_hint_response(
            out, outCap, "Chest reward configuration is invalid");

    before = *role;
    projected = before;
    if (!vm_net_mock_role_consume_backpack_item(
            &projected, chest->chestItemId, chestItem->seq, 1,
            &chestRemaining) ||
        !vm_net_mock_role_consume_backpack_item(
            &projected, chest->keyItemId, keyItem->seq, 1, &keyRemaining) ||
        !vm_net_mock_role_add_backpack_item_to_role_in_memory(
            &projected, reward->itemId, reward->count, &rewardSeq) ||
        rewardSeq == 0)
    {
        return vm_net_mock_build_item_use_hint_response(
            out, outCap, "Backpack cannot receive chest reward");
    }
    rewardItem = vm_net_mock_role_find_backpack_item(&projected,
                                                      reward->itemId, rewardSeq);
    rewardWireCount = vm_net_mock_backpack_item_id_uses_reservoir_count(
                          reward->itemId)
                          ? (rewardItem ? rewardItem->count : 0)
                          : reward->count;
    if (rewardItem == NULL || rewardWireCount == 0)
        return vm_net_mock_build_item_use_hint_response(
            out, outCap, "Chest reward state is invalid");

    itemUseType = parsed.itemUseType ? parsed.itemUseType : 1;
    if (!vm_net_mock_begin_wt_object(out, outCap, &pos, 1, 7, 1,
                                     &objectStart) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "result", 1) ||
        !vm_net_mock_put_object_u8(out, outCap, &pos, "type", itemUseType) ||
        !vm_net_mock_put_object_u16(out, outCap, &pos, "id",
                                    (u16)chest->chestItemId))
    {
        return 0;
    }
    vm_net_mock_finish_wt_object(out, objectStart, pos);
    ++objectCount;
    if (!vm_net_mock_append_backpack_item_remove7_objects(
            out, outCap, &pos, &objectCount, chestItem->seq,
            chest->chestItemId, chestRemaining) ||
        !vm_net_mock_append_backpack_item_remove7_objects(
            out, outCap, &pos, &objectCount, keyItem->seq,
            chest->keyItemId, keyRemaining) ||
        !vm_net_mock_append_backpack_item_add7_object(
            out, outCap, &pos, rewardSeq, reward->itemId, rewardWireCount))
    {
        return 0;
    }
    ++objectCount;
    if (!vm_net_mock_append_chest_open_reward_hint_object(
            out, outCap, &pos, &objectCount, chest->chestItemId,
            rewardCatalogItem->name, reward->count))
    {
        return vm_net_mock_build_item_use_hint_response(
            out, outCap, "Chest reward notice is invalid");
    }
    vm_net_mock_finish_wt_packet(out, pos, objectCount);

    *role = projected;
    if (!vm_net_mock_role_db_save("chest-open"))
    {
        *role = before;
        return vm_net_mock_build_item_use_hint_response(
            out, outCap, "Chest opening could not be saved");
    }
    if (reward->worldBroadcast &&
        !vm_mock_world_chat_publish_chest_reward(
            role->name[0] ? role->name : "Player",
            chest->chestItemId,
            vm_net_mock_chest_world_broadcast_name_gbk(chest->chestItemId),
            reward->itemId, rewardCatalogItem->name,
            reward->count))
    {
        /* The role transaction has committed at this point.  World notices
         * are a secondary durable channel, so do not claim an opening failed
         * or roll it back merely because that independent history write is
         * unavailable.  The warning records the first failed contract. */
        printf("[warn][mock-service] chest_world_broadcast_failed chest=%u reward=%u role=%u reason=world-chat-store-or-delivery\n",
               chest->chestItemId, reward->itemId, role->roleId);
    }
    printf("[info][network] mock_chest_open request=7/%u chest=%u key=%u chest_seq=%u key_seq=%u reward=%u reward_seq=%u count=%u weight=%u/%u draw=%u world_broadcast=%u response=7/1+2x(7/7-type2+7/11)+7/7-type1+16/2-notice evidence=item.dsh:522-524+JianghuOL.CBE:0x01033544+mmGame:0x11CE/0x0D04\n",
           requestSubtype, chest->chestItemId, chest->keyItemId, chestItem->seq,
           keyItem->seq, reward->itemId, rewardSeq, reward->count,
           reward->weight, totalWeight, draw,
           reward->worldBroadcast ? 1u : 0u);
    vm_autotest_note("mock_chest_open request=7/%u chest=%u key=%u chest_seq=%u key_seq=%u reward=%u reward_seq=%u count=%u weight=%u total_weight=%u world_broadcast=%u response=7/1+7/7-type2+7/11+7/7-type1+16/2-notice evidence=JianghuOL.CBE:0x01033544 mmGame:0x11CE/0x0D04\n",
                     requestSubtype, chest->chestItemId, chest->keyItemId,
                     chestItem->seq, keyItem->seq, reward->itemId, rewardSeq,
                     reward->count, reward->weight, totalWeight,
                     reward->worldBroadcast ? 1u : 0u);
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
    u8 itemInfo[VM_NET_MOCK_ITEM_USE_ITEMINFO_MAX_BYTES];
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

    /* A selected special item must be handled by its own client contract.
     * Returning no generic response here intentionally leaves an unexpected
     * 7/1 variant observable instead of consuming it as a false success. */
    if (vm_net_mock_item_requires_special_use_protocol(itemId))
        return 0;

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
            /*
             * The normal backpack discard action identifies one selected
             * entry but has no count field.  It means "discard one", not
             * "discard this row's entire stack".  A batch caller must state
             * a positive count explicitly; zero then reaches the normal
             * consume validation and returns result=2.
             */
            discardCount = parsed.haveCount ? parsed.count : 1;
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
    printf("[info][network] mock_item_discard item=%u seq=%u count=%u request_count=%s remaining=%u result=%u refresh=%s resp=%u\n",
           itemId, seq, discardCount, parsed.haveCount ? "explicit" : "default-one", remaining, result,
           consumed ? "7/4+17/1+7/42+7/11" : "7/4-fail", pos);
    vm_autotest_note("mock_item_discard item=%u seq=%u count=%u request_count=%s remaining=%u result=%u response=%s evidence=runtime:wt7/4 JianghuOL.CBE:0x1033544 mmGame:0x418C\n",
                     itemId, seq, discardCount, parsed.haveCount ? "explicit" : "default-one", remaining, result,
                     consumed ? "7/4+17/1+7/42+7/11" : "7/4-fail");
    return pos;
}

static bool vm_net_mock_append_backpack_items_object_with_stage_attrs(
    u8 *out, u32 outCap, u32 *pos, bool includeStageAttrs)
{
    u32 objectStart = 0;
    u8 itemInfo[VM_NET_MOCK_BACKPACK_CLIENT_ITEMINFO_MAX_BYTES];
    u32 itemInfoLen = 0;
    u32 rowCount = 0;
    vm_net_mock_role_state *role = vm_net_mock_active_role();
    u16 capacity = vm_net_mock_backpack_client_capacity(
        role ? role->backpackCapacity : VM_NET_MOCK_BACKPACK_INITIAL_CAPACITY);

    if (out == NULL || pos == NULL)
        return false;
    memset(itemInfo, 0, sizeof(itemInfo));
    if (!vm_net_mock_build_backpack_iteminfo_blob_with_stage_attrs(
            itemInfo, sizeof(itemInfo), role, includeStageAttrs,
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

    printf("[info][network] mock_backpack_items role=%u capacity=%u rows=%u stored_rows=%u iteminfo_len=%u layout=%s\n",
           role ? role->roleId : 0,
           capacity,
           rowCount,
           vm_net_mock_role_backpack_count(role),
           itemInfoLen, includeStageAttrs ? "detail" : "bootstrap-compact");
    vm_autotest_note("mock_backpack_items role=%u capacity=%u rows=%u stored_rows=%u iteminfo_len=%u layout=%s evidence=mmGame:0x418C+mmShop:sub_9DE\n",
                     role ? role->roleId : 0,
                     capacity,
                     rowCount,
                     vm_net_mock_role_backpack_count(role),
                     itemInfoLen, includeStageAttrs ? "detail" : "bootstrap-compact");
    return true;
}

static bool vm_net_mock_append_backpack_items_object(u8 *out, u32 outCap,
                                                      u32 *pos)
{
    return vm_net_mock_append_backpack_items_object_with_stage_attrs(
        out, outCap, pos, true);
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
    u8 itemInfo[VM_NET_MOCK_BACKPACK_GRID_ITEMINFO_MAX_BYTES];
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
    u8 itemInfo[VM_NET_MOCK_EQUIPMENT_LOGIN_ITEMINFO_MAX_BYTES];
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

    /* A zero-row 7/7 type=2 is not an equipment-list initializer.  The
     * mmGame parser dispatches type=2 to a parameterless operation on the
     * current selected item, so emitting it after a 30/21 backpack grid can
     * mutate that newly selected row despite there being no equipped row to
     * describe.  With no equipped rows the correct response is no 7/7
     * object at all. */
    if (rowCount == 0)
        return true;

    /* A non-empty worn-equipment reply remains an unresolved contract: the
     * native mmGame type=2 branch is a parameterless selected-item operation,
     * not a row-consuming equipment-list initializer.  Preserve the legacy
     * packet only for existing worn roles until its real login protocol is
     * traced; never emit a fabricated zero-row form. */
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

        if (vm_net_mock_role_backpack_client_grid_count(role) != 0)
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
        if (equipmentRows != 0)
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
static char g_vm_net_mock_scene_moveinfo_npc_seeded_scene[64];
static bool g_vm_net_mock_scene_moveinfo_npc_seeded = false;

static bool vm_net_mock_read_current_player_grid(u32 *nodeOut, u32 *actorIdOut,
                                                 u16 *gridXOut, u16 *gridYOut,
                                                 u16 *targetXOut, u16 *targetYOut);
static bool vm_net_mock_snapshot_current_player_pos(const char *reason);
static bool vm_net_mock_scene_names_equal_exact(const char *a, const char *b);

static void vm_net_mock_reset_scene_moveinfo_npc_seed_if_needed(const char *scene)
{
    if (g_vm_net_mock_scene_moveinfo_npc_seeded &&
        (scene == NULL ||
         g_vm_net_mock_scene_moveinfo_npc_seeded_scene[0] == 0 ||
         !vm_net_mock_scene_names_equal_exact(g_vm_net_mock_scene_moveinfo_npc_seeded_scene,
                                              scene)))
    {
        g_vm_net_mock_scene_moveinfo_npc_seeded = false;
        g_vm_net_mock_scene_moveinfo_npc_seeded_scene[0] = 0;
    }
    if (g_vm_net_mock_scene_moveinfo_npc_pending &&
        (scene == NULL ||
         g_vm_net_mock_scene_moveinfo_npc_pending_scene[0] == 0 ||
         !vm_net_mock_scene_names_equal_exact(g_vm_net_mock_scene_moveinfo_npc_pending_scene,
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
        !vm_net_mock_scene_names_equal_exact(g_vm_net_mock_scene_moveinfo_npc_pending_scene,
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
        "../web/fs/JHOnlineData/%s",
        "web/fs/JHOnlineData/%s"
    };
    char candidate[1200];

    if (fpOut)
        *fpOut = NULL;
    if (pathOut && pathOutCap != 0)
        pathOut[0] = 0;
    if (scene == NULL || scene[0] == 0 ||
        !vm_net_mock_str_ends_with(scene, ".sce") ||
        vm_net_mock_scene_name_has_path_separator(scene))
    return false;

    if (g_vm_net_mock_resource_dir[0] != 0)
    {
        FILE *fp = NULL;
        if (vm_net_mock_build_configured_resource_path(scene, candidate,
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
        FILE *fp = NULL;
        snprintf(candidate, sizeof(candidate), pathFormats[i], scene);
        fp = vm_net_mock_fopen_game_path(candidate, "rb");
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
    return false;
}

static bool vm_net_mock_open_server_data_resource(const char *name,
                                                  const char *requiredSuffix,
                                                  FILE **fpOut,
                                                  char *pathOut,
                                                  size_t pathOutCap);

/* Older service builds persisted some role scenes as bare basenames.  This
 * is a one-time data repair, not a request-time alias: only an exact raw row
 * from the authoritative sMap.dsh can replace such a legacy value. */
static bool vm_net_mock_scene_key_resolve_exact_smap(const char *legacyScene,
                                                     char *exactScene,
                                                     size_t exactSceneCap)
{
    char path[256];
    u8 data[16384];
    u32 len = 0;
    u32 columnCount = 0;
    u32 rowCount = 0;
    u32 headerBytes = 0;
    u32 pos = 0;
    size_t legacyLen = 0;
    bool found = false;

    if (exactScene == NULL || exactSceneCap == 0)
        return false;
    exactScene[0] = 0;
    if (legacyScene == NULL || legacyScene[0] == 0 ||
        vm_net_mock_str_ends_with(legacyScene, ".sce") ||
        !vm_net_mock_scene_name_is_download_key(legacyScene))
    {
        return false;
    }
    legacyLen = strlen(legacyScene);
    if (legacyLen + 4 >= exactSceneCap ||
        !vm_net_mock_open_server_data_resource("sMap.dsh", ".dsh", NULL,
                                               path, sizeof(path)))
    {
        return false;
    }
    len = vm_net_mock_load_response_file(path, data, sizeof(data));
    if (len < 20 || vm_net_mock_read_le32_at(data, 0) != len - 4)
        return false;
    columnCount = vm_net_mock_read_le32_at(data, 4);
    rowCount = vm_net_mock_read_le32_at(data, 8);
    headerBytes = vm_net_mock_read_le32_at(data, 12);
    if (columnCount < 2 || columnCount > 64 || rowCount > 512 ||
        16u + headerBytes > len)
    {
        return false;
    }
    pos = 16u + headerBytes;
    for (u32 row = 0; row < rowCount && pos + 4 <= len; ++row)
    {
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowPos = pos + 4;
        u32 rowEnd = rowPos + rowLen;

        if (rowLen == 0 || rowEnd > len || rowEnd < rowPos)
            return false;
        for (u32 column = 0; column < columnCount && rowPos < rowEnd; ++column)
        {
            u32 valueLen = data[rowPos++];
            const u8 *value = data + rowPos;
            u32 nameLen = valueLen;

            if (rowPos + valueLen > rowEnd)
                return false;
            if (column == 1)
            {
                while (nameLen > 0 && value[nameLen - 1] == 0)
                    --nameLen;
                if (nameLen == legacyLen + 4 &&
                    memcmp(value, legacyScene, legacyLen) == 0 &&
                    memcmp(value + legacyLen, ".sce", 4) == 0)
                {
                    if (found || nameLen >= exactSceneCap)
                    {
                        /* Multiple sMap rows with the same old basename are
                         * ambiguous.  Preserve it as unresolved rather than
                         * selecting an arbitrary map identity. */
                        exactScene[0] = 0;
                        return false;
                    }
                    memcpy(exactScene, value, nameLen);
                    exactScene[nameLen] = 0;
                    found = true;
                }
            }
            rowPos += valueLen;
        }
        pos = rowEnd;
    }
    return found;
}

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

    /* Scene identity and resource lookup are byte-for-byte.  In particular,
     * `foo` never means `foo.sce`, and c00蓬莱仙岛_02.sce,
     * 00蓬莱仙岛_02.sce and 00_蓬莱仙岛02.sce are distinct keys. */
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
    return vm_net_mock_scene_name_is_download_key(scene) &&
           vm_net_mock_str_ends_with(scene, ".sce");
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
    /* Historical callers already validate their scene source before entering
     * this boundary.  Keep that invariant explicit: an invalid value becomes
     * unresolved, never the bootstrap scene and never an extension alias. */
    if (!vm_net_mock_scene_name_is_persistable(scene))
        return "";
    /* A scene-enter key is a resource/map-controller identity, not a lookup
     * hint.  Do not strip, append, or otherwise rewrite it. */
    return scene;
}

static bool vm_net_mock_scene_names_equal_exact(const char *a, const char *b)
{
    return a != NULL && b != NULL && a[0] != 0 && b[0] != 0 &&
           strcmp(a, b) == 0;
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

