/*
 * Deterministic server-only regression for equipment enhancement effects.
 *
 * It does not start a listener, contact MySQL, or mutate a role.  The test
 * covers the same authoritative helpers used by role-stat aggregation and
 * the same four-stage attribute plan used by equipment object serialization.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static const u32 kBase100Cumulative[17] = {
    0, 12, 25, 39, 54, 75, 99, 122, 148,
    176, 207, 237, 270, 312, 359, 411, 467};

static int expect_title_rule_table_wire_contract(void)
{
    u8 data[VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL * 7];
    u8 fields[160];
    u8 packet[512];
    const u8 *wireData = NULL;
    const u8 *wireNum = NULL;
    u16 wireDataLen = 0;
    u16 wireNumLen = 0;
    u32 dataLen = 0;
    u32 fieldsLen = 0;
    u32 packetLen = 0;
    u32 firstObjectLen = 0;
    u32 secondObjectStart = 0;
    u32 secondObjectLen = 0;

    memset(data, 0, sizeof(data));
    memset(fields, 0, sizeof(fields));
    memset(packet, 0, sizeof(packet));
    dataLen = vm_net_mock_build_equipment_enhance_primary_rule_data(
        data, sizeof(data));
    if (dataLen != sizeof(data))
    {
        fputs("title enhancement rule data length changed\n", stderr);
        return 1;
    }
    for (u8 i = 0; i < VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL; ++i)
    {
        const vm_net_mock_equipment_enhance_primary_rule *rule =
            &g_vm_net_mock_equipment_enhance_primary_rules[i];
        const u8 *row = &data[i * 7u];

        if (row[0] != 0 || row[1] != 1 || row[2] != rule->flat ||
            row[3] != 0 || row[4] != 2 || row[5] != 0 ||
            row[6] != rule->percent)
        {
            fprintf(stderr, "title enhancement rule row %u changed\n", i);
            return 1;
        }
    }

    if (!vm_net_mock_append_title_equipment_enhance_rules(
            fields, sizeof(fields), &fieldsLen) ||
        fieldsLen != 128 ||
        fields[0] != 3 || memcmp(&fields[1], "num", 3) != 0 ||
        fields[4] != 0 || fields[5] != 3 ||
        fields[6] != 0 || fields[7] != 1 ||
        fields[8] != VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL ||
        fields[9] != 4 || memcmp(&fields[10], "data", 4) != 0 ||
        fields[14] != 0 || fields[15] != sizeof(data) ||
        memcmp(&fields[16], data, sizeof(data)) != 0)
    {
        fputs("title subtype-4 num/data fields changed\n", stderr);
        return 1;
    }

    /* Exercise the complete staged role-list builder without contacting
     * MySQL.  An empty but valid loaded role DB still produces the native
     * actorinfo count row and lets us inspect the exact subtype-4 object. */
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    memset(&g_vm_net_mock_role_db, 0, sizeof(g_vm_net_mock_role_db));
    packetLen = vm_net_mock_build_title_rolelist_stage_response(
        packet, sizeof(packet));
    if (packetLen >= 11)
    {
        firstObjectLen = ((u32)packet[9] << 8) | packet[10];
        secondObjectStart = 5 + firstObjectLen;
    }
    if (secondObjectStart + 6 <= packetLen)
        secondObjectLen = ((u32)packet[secondObjectStart + 4] << 8) |
                          packet[secondObjectStart + 5];
    if (packetLen == 0 || packet[4] != 2 ||
        firstObjectLen < 6 || 5 + firstObjectLen > packetLen ||
        packet[5] != 1 || packet[6] != 1 || packet[7] != 16 ||
        secondObjectLen < 6 ||
        secondObjectStart + secondObjectLen != packetLen ||
        packet[secondObjectStart] != 1 ||
        packet[secondObjectStart + 1] != 1 ||
        packet[secondObjectStart + 2] != 4 ||
        !vm_net_mock_get_object_entry_bytes(
            packet + secondObjectStart + 6, secondObjectLen - 6,
            "num", &wireNum, &wireNumLen) ||
        wireNumLen != 3 || wireNum[0] != 0 || wireNum[1] != 1 ||
        wireNum[2] != VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL ||
        !vm_net_mock_get_object_entry_bytes(
            packet + secondObjectStart + 6, secondObjectLen - 6,
            "data", &wireData, &wireDataLen) ||
        wireDataLen != sizeof(data) || memcmp(wireData, data, sizeof(data)) != 0)
    {
        fprintf(stderr,
                "complete title subtype-4 response omitted rule fields: "
                "packet=%u count=%u first=%u second_start=%u second=%u "
                "num=%u data=%u\n",
                packetLen, packet[4], firstObjectLen, secondObjectStart,
                secondObjectLen, wireNumLen, wireDataLen);
        return 1;
    }
    return 0;
}

static int expect_ring_does_not_infer_a_primary(void)
{
    vm_net_mock_equipment_catalog_item ring;
    vm_net_mock_equipment_enhance_affix_state affixes;
    vm_net_mock_equipment_enhance_attr attrs[5];
    vm_net_mock_equipment_bonus bonus;

    memset(&ring, 0, sizeof(ring));
    memset(&affixes, 0, sizeof(affixes));
    ring.slot = 7;
    ring.category = 6;
    ring.bonus.mp = 652;
    ring.bonus.crit = 16;

    /* Category 6 follows the firmware armour branch.  This ring has no base
     * armour, so neither its client attribute array nor authoritative stats
     * may silently reinterpret MP or critical chance as the primary. */
    if (vm_net_mock_equipment_enhancement_collect_attrs(
            &ring, 1, &affixes, attrs,
            (u8)(sizeof(attrs) / sizeof(attrs[0]))) != 0)
    {
        fputs("base enhancement leaked into the client stage-attribute array\n",
              stderr);
        return 1;
    }

    memset(&bonus, 0, sizeof(bonus));
    vm_net_mock_equipment_enhancement_add_bonus(&ring, 1, &affixes, &bonus);
    if (bonus.mp != 0 || bonus.armor != 0 || bonus.attack != 0 ||
        bonus.crit != 0)
    {
        fputs("ring +1 inferred a non-firmware primary attribute\n", stderr);
        return 1;
    }

    memset(&bonus, 0, sizeof(bonus));
    vm_net_mock_equipment_enhancement_add_bonus(&ring, 2, &affixes, &bonus);
    if (bonus.mp != 0 || bonus.armor != 0 || bonus.attack != 0 ||
        bonus.crit != 0)
    {
        fputs("ring +2 inferred a non-firmware primary attribute\n", stderr);
        return 1;
    }
    return 0;
}

static int expect_shipped_catalog_native_primary_rule(void)
{
    static const struct
    {
        u32 itemId;
        bool resolved;
        u8 type;
        u32 base;
    } cases[] = {
        {1001, true, VM_NET_MOCK_EQUIP_ATTR_ATTACK, 30},
        {1501, true, VM_NET_MOCK_EQUIP_ATTR_ATTACK, 30},
        {2001, true, VM_NET_MOCK_EQUIP_ATTR_ATTACK, 15},
        {3001, true, VM_NET_MOCK_EQUIP_ATTR_ARMOR, 45},
        {8001, true, VM_NET_MOCK_EQUIP_ATTR_ARMOR, 10},
        {7001, false, 0, 0},
        {9001, false, 0, 0},
    };

    for (u32 i = 0; i < (u32)(sizeof(cases) / sizeof(cases[0])); ++i)
    {
        const vm_net_mock_equipment_catalog_item *equipment =
            vm_net_mock_find_equipment_catalog_item(cases[i].itemId);
        u8 type = 0;
        u32 base = 0;

        bool resolved = equipment != NULL &&
                        vm_net_mock_equipment_enhancement_resolve_primary(
                            equipment, &type, &base);

        if (equipment == NULL || resolved != cases[i].resolved ||
            (resolved &&
             (type != cases[i].type || base != cases[i].base)))
        {
            fprintf(stderr,
                    "shipped equipment native primary mismatch item=%u "
                    "got=%u:%u/%u expected=%u:%u/%u\n",
                    cases[i].itemId, resolved, type, base,
                    cases[i].resolved, cases[i].type, cases[i].base);
            return 1;
        }
    }
    return 0;
}

static int expect_native_primary_curve_every_level(void)
{
    vm_net_mock_equipment_catalog_item sword;
    vm_net_mock_equipment_catalog_item dagger;
    vm_net_mock_equipment_catalog_item staff;
    vm_net_mock_equipment_catalog_item armour;
    vm_net_mock_equipment_catalog_item cloak;
    vm_net_mock_equipment_catalog_item belt;
    vm_net_mock_equipment_catalog_item ring;
    vm_net_mock_equipment_enhance_affix_state affixes;

    memset(&sword, 0, sizeof(sword));
    memset(&dagger, 0, sizeof(dagger));
    memset(&staff, 0, sizeof(staff));
    memset(&armour, 0, sizeof(armour));
    memset(&cloak, 0, sizeof(cloak));
    memset(&belt, 0, sizeof(belt));
    memset(&ring, 0, sizeof(ring));
    memset(&affixes, 0, sizeof(affixes));
    sword.slot = 0;
    sword.category = 7;
    sword.bonus.attack = 100;
    sword.bonus.strength = 50;
    dagger.slot = 0;
    dagger.category = 8;
    dagger.bonus.attack = 100;
    dagger.bonus.agility = 40;
    staff.slot = 0;
    staff.category = 9;
    staff.bonus.attack = 100;
    staff.bonus.wisdom = 150;
    armour.slot = 2;
    armour.category = 1;
    armour.bonus.armor = 100;
    armour.bonus.hp = 40;
    cloak.slot = 3;
    cloak.category = 2;
    cloak.bonus.armor = 100;
    cloak.bonus.hp = 150;
    cloak.bonus.mp = 120;
    belt.slot = 4;
    belt.category = 3;
    belt.bonus.hp = 100;
    ring.slot = 7;
    ring.category = 6;
    ring.bonus.mp = 100;

    for (u8 level = 0; level <= VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL; ++level)
    {
        vm_net_mock_equipment_bonus swordBonus;
        vm_net_mock_equipment_bonus daggerBonus;
        vm_net_mock_equipment_bonus staffBonus;
        vm_net_mock_equipment_bonus armourBonus;
        vm_net_mock_equipment_bonus cloakBonus;
        vm_net_mock_equipment_bonus beltBonus;
        vm_net_mock_equipment_bonus ringBonus;

        memset(&swordBonus, 0, sizeof(swordBonus));
        memset(&daggerBonus, 0, sizeof(daggerBonus));
        memset(&staffBonus, 0, sizeof(staffBonus));
        memset(&armourBonus, 0, sizeof(armourBonus));
        memset(&cloakBonus, 0, sizeof(cloakBonus));
        memset(&beltBonus, 0, sizeof(beltBonus));
        memset(&ringBonus, 0, sizeof(ringBonus));
        vm_net_mock_equipment_enhancement_add_bonus(
            &sword, level, &affixes, &swordBonus);
        vm_net_mock_equipment_enhancement_add_bonus(
            &dagger, level, &affixes, &daggerBonus);
        vm_net_mock_equipment_enhancement_add_bonus(
            &staff, level, &affixes, &staffBonus);
        vm_net_mock_equipment_enhancement_add_bonus(
            &armour, level, &affixes, &armourBonus);
        vm_net_mock_equipment_enhancement_add_bonus(
            &cloak, level, &affixes, &cloakBonus);
        vm_net_mock_equipment_enhancement_add_bonus(
            &belt, level, &affixes, &beltBonus);
        vm_net_mock_equipment_enhancement_add_bonus(
            &ring, level, &affixes, &ringBonus);
        if (swordBonus.attack != kBase100Cumulative[level] ||
            swordBonus.strength != 0 ||
            daggerBonus.attack != kBase100Cumulative[level] ||
            daggerBonus.agility != 0 ||
            staffBonus.attack != kBase100Cumulative[level] ||
            staffBonus.wisdom != 0 ||
            armourBonus.armor != kBase100Cumulative[level] ||
            armourBonus.hp != 0 ||
            cloakBonus.armor != kBase100Cumulative[level] ||
            cloakBonus.hp != 0 || cloakBonus.mp != 0 ||
            beltBonus.hp != 0 || beltBonus.armor != 0 ||
            ringBonus.mp != 0 || ringBonus.armor != 0)
        {
            fprintf(stderr, "native attack/armour curve mismatch at +%u\n",
                    level);
            return 1;
        }
        if (level != 0 &&
            kBase100Cumulative[level] <= kBase100Cumulative[level - 1])
        {
            fprintf(stderr, "primary curve did not grow at +%u\n", level);
            return 1;
        }
    }
    return 0;
}

static int expect_four_stage_activation(void)
{
    static const u8 types[4] = {
        VM_NET_MOCK_EQUIP_ATTR_CRIT, VM_NET_MOCK_EQUIP_ATTR_ATTACK,
        VM_NET_MOCK_EQUIP_ATTR_HIT, VM_NET_MOCK_EQUIP_ATTR_WISDOM};
    static const u16 values[4] = {11, 22, 33, 44};
    vm_net_mock_equipment_catalog_item weapon;
    vm_net_mock_equipment_enhance_affix_state affixes;
    vm_net_mock_equipment_enhance_affix_state unpacked;
    vm_net_mock_equipment_enhance_attr attrs[5];

    memset(&weapon, 0, sizeof(weapon));
    memset(&affixes, 0, sizeof(affixes));
    memset(&unpacked, 0, sizeof(unpacked));
    weapon.itemId = 1001;
    weapon.slot = 0;
    weapon.category = 7;
    weapon.levelRequired = 20;
    weapon.bonus.attack = 100;
    weapon.bonus.strength = 50;
    memcpy(affixes.type, types, sizeof(types));
    memcpy(affixes.value, values, sizeof(values));

    if (vm_net_mock_equipment_enhancement_ensure_affixes(
            &weapon, 0, &affixes, 0x12345678u))
    {
        fputs("valid persisted affix plan was rerolled\n", stderr);
        return 1;
    }
    vm_net_mock_equipment_enhancement_unpack_affixes(
        &unpacked,
        vm_net_mock_equipment_enhancement_pack_affix_types(&affixes),
        vm_net_mock_equipment_enhancement_pack_affix_values(&affixes));
    if (memcmp(&affixes, &unpacked, sizeof(affixes)) != 0)
    {
        fputs("affix persistence pack/unpack changed the four-stage plan\n",
              stderr);
        return 1;
    }

    memset(attrs, 0, sizeof(attrs));
    if (vm_net_mock_equipment_enhancement_collect_wire_attrs(
            &weapon, 0, &affixes, attrs,
            (u8)(sizeof(attrs) / sizeof(attrs[0]))) != 4)
    {
        fputs("initial equipment object did not carry all future stages\n",
              stderr);
        return 1;
    }
    for (u8 stage = 0; stage < 4; ++stage)
    {
        if (attrs[stage].threshold != (u8)((stage + 1u) * 4u) ||
            attrs[stage].type != types[stage] ||
            attrs[stage].value != values[stage])
        {
            fprintf(stderr, "wire stage %u changed\n", stage);
            return 1;
        }
    }

    for (u8 level = 0; level <= VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL; ++level)
    {
        vm_net_mock_equipment_bonus bonus;
        u8 expectedCount = (u8)(level / 4u);
        u8 count = 0;

        memset(attrs, 0, sizeof(attrs));
        memset(&bonus, 0, sizeof(bonus));
        count = vm_net_mock_equipment_enhancement_collect_attrs(
            &weapon, level, &affixes, attrs,
            (u8)(sizeof(attrs) / sizeof(attrs[0])));
        if (count != expectedCount)
        {
            fprintf(stderr, "stage count mismatch at +%u: got %u expected %u\n",
                    level, count, expectedCount);
            return 1;
        }
        for (u8 stage = 0; stage < count; ++stage)
        {
            if (attrs[stage].threshold != (u8)((stage + 1u) * 4u) ||
                attrs[stage].type != types[stage] ||
                attrs[stage].value != values[stage])
            {
                fprintf(stderr, "active stage %u changed at +%u\n", stage,
                        level);
                return 1;
            }
        }

        vm_net_mock_equipment_enhancement_add_bonus(
            &weapon, level, &affixes, &bonus);
        if (bonus.attack != kBase100Cumulative[level] +
                                (level >= 8 ? values[1] : 0) ||
            bonus.strength != 0 ||
            bonus.crit != (level >= 4 ? values[0] : 0) ||
            bonus.hit != (level >= 12 ? values[2] : 0) ||
            bonus.wisdom != (level >= 16 ? values[3] : 0))
        {
            fprintf(stderr, "stage bonus mismatch at +%u\n", level);
            return 1;
        }
    }
    return 0;
}

static int expect_non_native_primary_does_not_fall_back(void)
{
    vm_net_mock_equipment_catalog_item staff;
    vm_net_mock_equipment_catalog_item cloak;
    vm_net_mock_equipment_enhance_affix_state affixes;
    vm_net_mock_equipment_bonus bonus;

    memset(&staff, 0, sizeof(staff));
    memset(&cloak, 0, sizeof(cloak));
    memset(&affixes, 0, sizeof(affixes));
    staff.slot = 0;
    staff.category = 9;
    staff.bonus.attack = 45;
    cloak.slot = 3;
    cloak.category = 2;
    cloak.bonus.hp = 100;

    memset(&bonus, 0, sizeof(bonus));
    vm_net_mock_equipment_enhancement_add_bonus(&staff, 2, &affixes, &bonus);
    if (bonus.attack != 13 || bonus.wisdom != 0)
    {
        fputs("staff did not use its firmware attack primary\n", stderr);
        return 1;
    }

    memset(&bonus, 0, sizeof(bonus));
    vm_net_mock_equipment_enhancement_add_bonus(&cloak, 2, &affixes, &bonus);
    if (bonus.hp != 0 || bonus.armor != 0)
    {
        fputs("cloak without armour fell back to a non-firmware primary\n",
              stderr);
        return 1;
    }
    return 0;
}

static int expect_new_plan_is_complete_and_stable(void)
{
    vm_net_mock_equipment_catalog_item equipment;
    vm_net_mock_equipment_enhance_affix_state generated;
    vm_net_mock_equipment_enhance_affix_state snapshot;

    memset(&equipment, 0, sizeof(equipment));
    memset(&generated, 0, sizeof(generated));
    equipment.itemId = 1002;
    equipment.slot = 7;
    equipment.category = 6;
    equipment.levelRequired = 20;

    if (!vm_net_mock_equipment_enhancement_ensure_affixes(
            &equipment, 0, &generated, 0x87654321u))
    {
        fputs("empty equipment instance did not generate an affix plan\n",
              stderr);
        return 1;
    }
    snapshot = generated;
    for (u8 i = 0; i < 4; ++i)
    {
        if (generated.type[i] == 0 || generated.value[i] == 0)
        {
            fputs("generated affix plan is incomplete\n", stderr);
            return 1;
        }
        for (u8 j = 0; j < i; ++j)
        {
            if (generated.type[i] == generated.type[j])
            {
                fputs("generated affix plan contains duplicate types\n",
                      stderr);
                return 1;
            }
        }
    }
    if (vm_net_mock_equipment_enhancement_ensure_affixes(
            &equipment, 16, &generated, 0x11111111u) ||
        memcmp(&generated, &snapshot, sizeof(generated)) != 0)
    {
        fputs("persisted affix plan changed after later enhancement\n", stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (expect_title_rule_table_wire_contract() != 0 ||
        expect_shipped_catalog_native_primary_rule() != 0 ||
        expect_ring_does_not_infer_a_primary() != 0 ||
        expect_native_primary_curve_every_level() != 0 ||
        expect_non_native_primary_does_not_fall_back() != 0 ||
        expect_four_stage_activation() != 0 ||
        expect_new_plan_is_complete_and_stable() != 0)
    {
        return 1;
    }
    puts("equipment enhancement regression passed: title login initializes "
         "the native 16-level rule table; only weapon attack and non-weapon "
         "armour grow at every +1; +4/+8/+12/+16 stages activate exactly");
    return 0;
}
