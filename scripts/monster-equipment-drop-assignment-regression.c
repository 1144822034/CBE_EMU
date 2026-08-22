/*
 * Pure allocation regression for the monster-admin smart equipment action.
 *
 * Build from the repository root with the same command shape as
 * scripts/admin-request-length-regression.c.  This fixture does not start a
 * listener, connect to MySQL, or alter persistent state: it seeds the two
 * in-memory catalogs and exercises only the planner before its SQL layer.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

static void seed_shop_item(u32 index, u32 itemId, const char *name, bool equip)
{
    vm_net_mock_shop_catalog_item *item = &g_vm_net_mock_shop_catalog[index];

    memset(item, 0, sizeof(*item));
    item->itemId = itemId;
    item->isEquip = equip ? 1 : 0;
    snprintf(item->name, sizeof(item->name), "%s", name);
}

static void seed_equipment(u32 index, u32 itemId, u32 level, u32 quality)
{
    vm_net_mock_equipment_catalog_item *item =
        &g_vm_net_mock_equipment_catalog[index];

    memset(item, 0, sizeof(*item));
    item->itemId = itemId;
    item->slot = (u8)(index % VM_NET_MOCK_EQUIP_SLOT_COUNT);
    item->levelRequired = (u8)level;
    item->quality = (u8)quality;
}

static void seed_equipment_in_slot(u32 index, u32 itemId, u32 level,
                                   u32 quality, u32 slot)
{
    vm_net_mock_equipment_catalog_item *item =
        &g_vm_net_mock_equipment_catalog[index];

    memset(item, 0, sizeof(*item));
    item->itemId = itemId;
    item->slot = (u8)slot;
    item->levelRequired = (u8)level;
    item->quality = (u8)quality;
}

static bool row_has_item(const vm_net_mock_monster_admin_row *row, u32 itemId)
{
    return vm_net_mock_monster_drop_row_has_item(row, itemId);
}

static u32 row_equipment_count(const vm_net_mock_monster_admin_row *row)
{
    u32 count = 0;

    for (u8 drop = 0; drop < row->dropCount; ++drop)
    {
        if (row->drops[drop].itemId >= 1001u)
            ++count;
    }
    return count;
}

static u16 row_drop_rate_basis_points(const vm_net_mock_monster_admin_row *row,
                                      u32 itemId)
{
    if (row == NULL)
        return 0;
    for (u8 drop = 0; drop < row->dropCount; ++drop)
    {
        if (row->drops[drop].itemId == itemId)
            return row->drops[drop].rateBasisPoints;
    }
    return 0;
}

int main(void)
{
    /* Each GBK name intentionally uses two or more shared Chinese tokens,
     * without relying on the source-file encoding of this regression. */
    static const char kBoar[] = "\xb0\xb1\xb0\xb2";
    static const char kBandit[] = "\xb0\xb3\xb0\xb4";
    static const char kWolf[] = "\xb0\xb5\xb0\xb6";
    static const char kBoarBoss[] = "\xb0\xb1\xb0\xb2\xb0\xb7";
    static const char kBanditBoss[] = "\xb0\xb3\xb0\xb4\xb0\xb8";
    static const char kBoarSword[] = "\xb0\xb1\xb0\xb2\xb0\xb9";
    static const char kBanditArmor[] = "\xb0\xb3\xb0\xb4\xb0\xba";
    static const char kWolfBoots[] = "\xb0\xb5\xb0\xb6\xb0\xbb";
    static const char kBoarBossCrown[] = "\xb0\xb1\xb0\xb2\xb0\xb7\xb0\xbc";
    static const char kBanditBossArmor[] = "\xb0\xb3\xb0\xb4\xb0\xb8\xb0\xbd";
    static const char kLowStaff[] = "\xb0\xb9\xb0\xba\xb0\xbb";
    vm_net_mock_monster_admin_row monsters[5];
    vm_net_mock_monster_equipment_drop_assignment assignment;
    const char *error = NULL;
    const u16 rates[3] = {125, 50, 25};
    const u32 equipmentIds[] = {1001, 1002, 1003, 2001, 2002, 3001, 3002};
    u16 rateBasisPoints = 0;
    char rateText[16];

    memset(rateText, 0, sizeof(rateText));
    if (!vm_net_mock_parse_drop_rate_basis_points("0.25", 4,
                                                   &rateBasisPoints) ||
        rateBasisPoints != 25 ||
        !vm_mock_admin_form_drop_rate_basis_points(
            "drop_rate_0=0.25", "drop_rate_0", &rateBasisPoints) ||
        rateBasisPoints != 25 ||
        vm_net_mock_parse_drop_rate_basis_points("0.001", 5,
                                                  &rateBasisPoints) ||
        vm_net_mock_parse_drop_rate_basis_points("100.01", 6,
                                                  &rateBasisPoints))
    {
        fputs("decimal drop-rate parsing failed\n", stderr);
        return 1;
    }
    vm_net_mock_format_drop_rate_basis_points(25, rateText, sizeof(rateText));
    if (strcmp(rateText, "0.25") != 0 ||
        vm_net_mock_battle_drop_count_for_battle(0, 1) != 0 ||
        vm_net_mock_battle_drop_count_for_battle(10000, 1) != 1)
    {
        fputs("decimal drop-rate formatting or battle boundaries failed\n",
              stderr);
        return 1;
    }

    memset(monsters, 0, sizeof(monsters));
    memset(&assignment, 0, sizeof(assignment));
    memset(g_vm_net_mock_monster_catalog_entries, 0,
           sizeof(g_vm_net_mock_monster_catalog_entries));
    memset(g_vm_net_mock_equipment_catalog, 0,
           sizeof(g_vm_net_mock_equipment_catalog));
    memset(g_vm_net_mock_shop_catalog, 0, sizeof(g_vm_net_mock_shop_catalog));
    g_vm_net_mock_monster_catalog_loaded = true;
    g_vm_net_mock_monster_catalog_loading = false;
    g_vm_net_mock_monster_catalog_count = 5;
    g_vm_net_mock_equipment_catalog_loaded = true;
    g_vm_net_mock_equipment_catalog_count = 8;
    g_vm_net_mock_shop_catalog_loaded = true;
    g_vm_net_mock_shop_catalog_count = 9;

    /* Monster 1 owns a task material.  The smart operation must retain its
     * current rate while replacing every other configured drop. */
    g_vm_net_mock_monster_catalog_entries[0] =
        (vm_net_mock_monster_entry){1, 10, VM_NET_MOCK_MONSTER_BEAST, 27, 77};
    g_vm_net_mock_monster_catalog_entries[1] =
        (vm_net_mock_monster_entry){4, 20, VM_NET_MOCK_MONSTER_BEAST, 0, 0};
    g_vm_net_mock_monster_catalog_entries[2] =
        (vm_net_mock_monster_entry){29, 30, VM_NET_MOCK_MONSTER_BEAST, 0, 0};
    g_vm_net_mock_monster_catalog_entries[3] =
        (vm_net_mock_monster_entry){23, 25, VM_NET_MOCK_MONSTER_BOSS, 0, 0};
    g_vm_net_mock_monster_catalog_entries[4] =
        (vm_net_mock_monster_entry){300, 35, VM_NET_MOCK_MONSTER_BOSS, 0, 0};

    monsters[0].enemyId = 1; monsters[0].level = 10;
    monsters[0].family = VM_NET_MOCK_MONSTER_BEAST;
    snprintf(monsters[0].displayName, sizeof(monsters[0].displayName), "%s", kBoar);
    monsters[0].dropCount = 1;
    monsters[0].drops[0] = (vm_net_mock_monster_drop){27, 7700};
    monsters[1].enemyId = 4; monsters[1].level = 20;
    monsters[1].family = VM_NET_MOCK_MONSTER_BEAST;
    snprintf(monsters[1].displayName, sizeof(monsters[1].displayName), "%s", kBandit);
    monsters[2].enemyId = 29; monsters[2].level = 30;
    monsters[2].family = VM_NET_MOCK_MONSTER_BEAST;
    snprintf(monsters[2].displayName, sizeof(monsters[2].displayName), "%s", kWolf);
    monsters[3].enemyId = 23; monsters[3].level = 25;
    monsters[3].family = VM_NET_MOCK_MONSTER_BOSS;
    snprintf(monsters[3].displayName, sizeof(monsters[3].displayName), "%s", kBoarBoss);
    monsters[4].enemyId = 300; monsters[4].level = 35;
    monsters[4].family = VM_NET_MOCK_MONSTER_BOSS;
    snprintf(monsters[4].displayName, sizeof(monsters[4].displayName), "%s", kBanditBoss);

    seed_shop_item(0, 27, "task", false);
    seed_shop_item(1, 1001, kBoarSword, true);
    seed_shop_item(2, 1002, kBanditArmor, true);
    seed_shop_item(3, 1003, kWolfBoots, true);
    seed_shop_item(4, 2001, kBoarBossCrown, true);
    seed_shop_item(5, 2002, kBanditBossArmor, true);
    seed_shop_item(6, 3001, kBoarBossCrown, true);
    seed_shop_item(7, 3002, kBanditBossArmor, true);
    seed_shop_item(8, 4001, kLowStaff, true);
    seed_equipment(0, 1001, 10, 0);
    seed_equipment(1, 1002, 20, 0);
    seed_equipment(2, 1003, 30, 0);
    seed_equipment(3, 2001, 25, 1);
    seed_equipment(4, 2002, 35, 1);
    seed_equipment(5, 3001, 25, 2);
    seed_equipment(6, 3002, 35, 2);
    /* No level 1-10 boss exists.  This quality-1 weapon must be skipped rather
     * than leaking upward to the level-25 or level-35 bosses. */
    seed_equipment(7, 4001, 6, 1);

    if (!vm_net_mock_monster_plan_equipment_drops(
            monsters, 5, rates, &assignment, &error))
    {
        fprintf(stderr, "assignment plan failed: %s\n", error ? error : "-");
        return 1;
    }
    if (assignment.equipmentByQuality[0] != 3 ||
        assignment.equipmentByQuality[1] != 2 ||
        assignment.equipmentByQuality[2] != 2 ||
        assignment.equipmentSkippedByQuality[1] != 1 ||
        assignment.taskDropsPreserved != 1 ||
        assignment.strongNameMatches != 7 ||
        !row_has_item(&monsters[0], 27) ||
        monsters[0].drops[0].rateBasisPoints != 7700 ||
        !row_has_item(&monsters[0], 1001) ||
        !row_has_item(&monsters[1], 1002) ||
        !row_has_item(&monsters[2], 1003) ||
        !row_has_item(&monsters[3], 2001) ||
        !row_has_item(&monsters[3], 3001) ||
        !row_has_item(&monsters[4], 2002) ||
        !row_has_item(&monsters[4], 3002) ||
        row_drop_rate_basis_points(&monsters[0], 1001) != 125 ||
        row_drop_rate_basis_points(&monsters[3], 2001) != 50 ||
        row_drop_rate_basis_points(&monsters[3], 3001) != 25 ||
        row_has_item(&monsters[3], 4001) ||
        row_has_item(&monsters[4], 4001))
    {
        fputs("assignment did not preserve task material or strong name matches\n", stderr);
        return 1;
    }
    for (u32 item = 0; item < sizeof(equipmentIds) / sizeof(equipmentIds[0]); ++item)
    {
        u32 occurrences = 0;

        for (u32 monster = 0; monster < 5; ++monster)
        {
            if (row_has_item(&monsters[monster], equipmentIds[item]))
                ++occurrences;
        }
        if (occurrences != 1)
        {
            fprintf(stderr, "equipment %u was assigned %u times\n",
                    equipmentIds[item], occurrences);
            return 1;
        }
    }
    if (row_equipment_count(&monsters[0]) != 1 ||
        row_equipment_count(&monsters[1]) != 1 ||
        row_equipment_count(&monsters[2]) != 1 ||
        row_equipment_count(&monsters[3]) != 2 ||
        row_equipment_count(&monsters[4]) != 2)
    {
        fputs("assignment is not evenly distributed within the eligible monsters\n", stderr);
        return 1;
    }

    /* A boss can carry one quality-1 and one quality-2 candidate for every
     * worn slot.  A second candidate of the same quality and slot must still
     * be skipped, so the full eight-slot fixture verifies both constraints. */
    {
        vm_net_mock_monster_admin_row bossQualityMonsters[1];
        vm_net_mock_monster_equipment_drop_assignment bossQualityAssignment;
        const char *bossQualityError = NULL;

        memset(bossQualityMonsters, 0, sizeof(bossQualityMonsters));
        memset(&bossQualityAssignment, 0, sizeof(bossQualityAssignment));
        memset(g_vm_net_mock_equipment_catalog, 0,
               sizeof(g_vm_net_mock_equipment_catalog));
        memset(g_vm_net_mock_shop_catalog, 0,
               sizeof(g_vm_net_mock_shop_catalog));
        g_vm_net_mock_equipment_catalog_count = 17;
        g_vm_net_mock_shop_catalog_count = 17;
        bossQualityMonsters[0].enemyId = 501;
        bossQualityMonsters[0].level = 25;
        bossQualityMonsters[0].family = VM_NET_MOCK_MONSTER_BOSS;
        for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
        {
            u32 qualityOneItem = 5100u + slot;
            u32 qualityTwoItem = 5200u + slot;

            seed_shop_item(slot * 2u, qualityOneItem, "quality-one", true);
            seed_shop_item(slot * 2u + 1u, qualityTwoItem, "quality-two", true);
            seed_equipment_in_slot(slot * 2u, qualityOneItem, 25, 1, slot);
            seed_equipment_in_slot(slot * 2u + 1u, qualityTwoItem, 25, 2,
                                   slot);
        }
        seed_shop_item(16, 5300, "quality-one-duplicate", true);
        seed_equipment_in_slot(16, 5300, 25, 1, 0);
        if (!vm_net_mock_monster_plan_equipment_drops(
                bossQualityMonsters, 1, rates, &bossQualityAssignment,
                &bossQualityError) ||
            bossQualityAssignment.equipmentByQuality[1] != 8 ||
            bossQualityAssignment.equipmentByQuality[2] != 8 ||
            bossQualityAssignment.equipmentSkippedByQuality[1] != 1 ||
            bossQualityAssignment.equipmentSkippedByQuality[2] != 0 ||
            row_equipment_count(&bossQualityMonsters[0]) != 16)
        {
            fprintf(stderr, "boss quality slot coexistence failed: %s\n",
                    bossQualityError ? bossQualityError : "-");
            return 1;
        }
        for (u32 slot = 0; slot < VM_NET_MOCK_EQUIP_SLOT_COUNT; ++slot)
        {
            if (!row_has_item(&bossQualityMonsters[0], 5100u + slot) ||
                !row_has_item(&bossQualityMonsters[0], 5200u + slot) ||
                row_has_item(&bossQualityMonsters[0], 5300))
            {
                fputs("boss quality slot coexistence or duplicate rejection failed\n",
                      stderr);
                return 1;
            }
        }
    }
    puts("monster equipment-drop assignment regression passed: decimal rates + task preserve + quality eligibility + level/name distribution + boss quality slot coexistence");
    return 0;
}
