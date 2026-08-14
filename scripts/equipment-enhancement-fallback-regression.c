/*
 * Regression for zero-native-primary equipment enhancement.
 *
 * It does not start a listener, contact MySQL, or mutate a role.  The test
 * uses the same catalog helpers that serialize common-extra and that the
 * equipped-only role-stat collector consumes.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

static int expect_fallback_ring(void)
{
    vm_net_mock_equipment_catalog_item ring;
    vm_net_mock_equipment_enhance_affix_state affixes;
    vm_net_mock_equipment_enhance_attr attrs[5];
    vm_net_mock_equipment_bonus bonus;
    u8 count = 0;

    memset(&ring, 0, sizeof(ring));
    memset(&affixes, 0, sizeof(affixes));
    memset(attrs, 0, sizeof(attrs));
    memset(&bonus, 0, sizeof(bonus));
    ring.slot = 7; /* ring: client-native non-weapon armour is zero */
    ring.bonus.mp = 652;
    ring.bonus.crit = 16;

    count = vm_net_mock_equipment_enhancement_collect_attrs(
        &ring, 1, &affixes, attrs, (u8)(sizeof(attrs) / sizeof(attrs[0])));
    if (count != 1 || attrs[0].threshold != 1 ||
        attrs[0].type != VM_NET_MOCK_EQUIP_ATTR_MP || attrs[0].mode != 0 ||
        attrs[0].value != 67)
    {
        fprintf(stderr, "fallback +1 did not encode MP +67\n");
        return 1;
    }
    vm_net_mock_equipment_enhancement_add_bonus(&ring, 1, &affixes, &bonus);
    if (bonus.mp != 67 || bonus.armor != 0 || bonus.attack != 0)
    {
        fprintf(stderr, "fallback +1 did not affect only MP\n");
        return 1;
    }

    memset(attrs, 0, sizeof(attrs));
    memset(&bonus, 0, sizeof(bonus));
    count = vm_net_mock_equipment_enhancement_collect_attrs(
        &ring, 2, &affixes, attrs, (u8)(sizeof(attrs) / sizeof(attrs[0])));
    vm_net_mock_equipment_enhancement_add_bonus(&ring, 2, &affixes, &bonus);
    if (count != 1 || attrs[0].value != 135 || bonus.mp != 135 ||
        bonus.armor != 0 || bonus.attack != 0)
    {
        fprintf(stderr, "fallback +2 did not retain cumulative MP growth\n");
        return 1;
    }
    return 0;
}

static int expect_native_primary_server_bonus(void)
{
    vm_net_mock_equipment_catalog_item weapon;
    vm_net_mock_equipment_catalog_item armour;
    vm_net_mock_equipment_enhance_affix_state affixes;
    vm_net_mock_equipment_enhance_attr attrs[5];
    vm_net_mock_equipment_bonus bonus;

    memset(&weapon, 0, sizeof(weapon));
    memset(&armour, 0, sizeof(armour));
    memset(&affixes, 0, sizeof(affixes));
    memset(attrs, 0, sizeof(attrs));
    memset(&bonus, 0, sizeof(bonus));
    weapon.slot = 0;
    weapon.bonus.attack = 100;
    armour.slot = 1;
    armour.bonus.armor = 100;

    if (vm_net_mock_equipment_enhancement_collect_attrs(
            &weapon, 1, &affixes, attrs,
            (u8)(sizeof(attrs) / sizeof(attrs[0]))) != 0)
    {
        fputs("native weapon received a duplicate fallback attribute\n", stderr);
        return 1;
    }
    vm_net_mock_equipment_enhancement_add_bonus(&weapon, 1, &affixes, &bonus);
    if (bonus.attack != 12 || bonus.armor != 0)
    {
        fputs("native weapon primary growth changed\n", stderr);
        return 1;
    }

    memset(&bonus, 0, sizeof(bonus));
    if (vm_net_mock_equipment_enhancement_collect_attrs(
            &armour, 1, &affixes, attrs,
            (u8)(sizeof(attrs) / sizeof(attrs[0]))) != 0)
    {
        fputs("native armour received a duplicate fallback attribute\n", stderr);
        return 1;
    }
    vm_net_mock_equipment_enhancement_add_bonus(&armour, 1, &affixes, &bonus);
    if (bonus.armor != 12 || bonus.attack != 0)
    {
        fputs("native armour primary growth changed\n", stderr);
        return 1;
    }
    return 0;
}

static int expect_server_primary_curve(void)
{
    /* Server battle aggregation uses the same deliberately designed
     * sixteen-stage curve, but it must not claim that this curve is encoded
     * in 29/1's material tables.  The CBE-side controller table remains a
     * separately investigated initialization contract. */
    if (vm_net_mock_equipment_enhancement_bonus_from_base(45, 2) != 13)
    {
        fputs("server +2 armour primary curve changed\n", stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (expect_fallback_ring() != 0 ||
        expect_native_primary_server_bonus() != 0 ||
        expect_server_primary_curve() != 0)
        return 1;
    puts("equipment enhancement regression passed: MP ring +67/+135; "
         "server armour +2 primary growth is +13");
    return 0;
}
