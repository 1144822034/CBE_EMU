/*
 * Regression for Battle Insight's full-backpack overflow rule.
 *
 * The test uses only a local role/catalog snapshot.  It does not start a
 * client or service, connect to MySQL, or write account data.  It proves that
 * a full bag sells the unreceived drop's value while preserving every existing
 * backpack row, including a strengthened equipment instance.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static void reset_catalog(void)
{
    memset(g_vm_net_mock_shop_catalog, 0, sizeof(g_vm_net_mock_shop_catalog));
    g_vm_net_mock_shop_catalog_loaded = true;
    g_vm_net_mock_shop_catalog_count = 2;
    g_vm_net_mock_shop_catalog[0].itemId = 304;
    g_vm_net_mock_shop_catalog[0].price = 101;
    g_vm_net_mock_shop_catalog[0].enabled = 1;
    g_vm_net_mock_shop_catalog[1].itemId = 305;
    g_vm_net_mock_shop_catalog[1].price = 0;
    g_vm_net_mock_shop_catalog[1].enabled = 1;
}

static void init_full_role(vm_net_mock_role_state *role)
{
    memset(role, 0, sizeof(*role));
    role->roleId = 700001;
    role->money = 500;
    role->backpackCapacity = 2;
    role->backpackItemCount = 2;
    role->backpackItems[0].itemId = 9001;
    role->backpackItems[0].seq = 11;
    role->backpackItems[0].count = 1;
    role->backpackItems[0].enhanceLevel = 15;
    role->backpackItems[1].itemId = 9002;
    role->backpackItems[1].seq = 12;
    role->backpackItems[1].count = 1;
}

int main(void)
{
    vm_net_mock_role_state role;
    vm_net_mock_role_state before;
    u32 unitSale = 0;
    u32 saleTotal = 0;

    reset_catalog();
    init_full_role(&role);
    before = role;

    if (!vm_net_mock_battle_insight_overflow_drop_requires_sale(&role, 304, 3))
    {
        fputs("full backpack did not require overflow-drop sale\n", stderr);
        return 1;
    }
    if (!vm_net_mock_battle_insight_apply_overflow_drop_sale(
            &role, 304, 3, &unitSale, &saleTotal) ||
        unitSale != 11 || saleTotal != 33 || role.money != 533 ||
        memcmp(role.backpackItems, before.backpackItems,
               sizeof(role.backpackItems)) != 0 ||
        role.backpackItemCount != before.backpackItemCount)
    {
        fputs("overflow sale changed existing backpack rows or credited the wrong value\n",
              stderr);
        return 1;
    }

    role = before;
    if (vm_net_mock_battle_insight_apply_overflow_drop_sale(
            &role, 305, 1, &unitSale, &saleTotal) ||
        memcmp(&role, &before, sizeof(role)) != 0)
    {
        fputs("unpriced overflow drop changed role state\n", stderr);
        return 1;
    }

    role = before;
    role.backpackCapacity = 3;
    if (vm_net_mock_battle_insight_overflow_drop_requires_sale(&role, 304, 1))
    {
        fputs("non-full backpack incorrectly required overflow sale\n", stderr);
        return 1;
    }

    puts("battle insight overflow-drop sale regression passed: drop-only sale, strengthened backpack preserved, unpriced/no-space guards");
    return 0;
}
