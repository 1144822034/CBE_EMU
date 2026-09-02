/* Deterministic regression for the level-aware PvE armour curve.
 *
 * The production mock service is included directly.  The test starts no
 * listener and makes no database or client I/O; it reads equip.dsh through the
 * same catalog path as normal default-monster generation.
 */

#include <stdint.h>
#include <stdio.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main
#include "../src/server/mock-server.c"

typedef struct
{
    u32 level;
    u32 rawDamage;
    u32 defense;
    u32 expectedDamage;
} pve_defense_case;

static int assert_pve_damage_properties(void)
{
    static const pve_defense_case cases[] = {
        {1u, 1000u, 0u, 1000u},
        {70u, 1000u, 250u, 944u},
        {70u, 1000u, 5000u, 501u},
        {70u, 1000u, 11735u, 346u},
        {70u, 1000u, 34500u, 229u},
        {70u, 1000u, UINT32_MAX, 150u},
    };
    u32 priorDamage = UINT32_MAX;

    if (vm_net_mock_pve_defense_breakpoint_for_level(1u) != 200u)
    {
        fputs("level-one PvE defense breakpoint changed\n", stderr);
        return 1;
    }
    if (vm_net_mock_pve_defense_breakpoint_for_level(70u) != 3521u)
    {
        fprintf(stderr, "level-70 PvE defense breakpoint changed: %u\n",
                vm_net_mock_pve_defense_breakpoint_for_level(70u));
        return 1;
    }
    for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        u32 actual = vm_net_mock_pve_damage_after_defense(
            cases[i].rawDamage, cases[i].defense, cases[i].level);

        if (actual != cases[i].expectedDamage || actual == 0 ||
            actual > cases[i].rawDamage ||
            (cases[i].level == 70u && actual > priorDamage))
        {
            fprintf(stderr,
                    "PvE mitigation mismatch raw=%u defense=%u level=%u actual=%u expected=%u\n",
                    cases[i].rawDamage, cases[i].defense, cases[i].level,
                    actual, cases[i].expectedDamage);
            return 1;
        }
        if (cases[i].level == 70u)
            priorDamage = actual;
        printf("pve_case level=%u raw=%u defense=%u damage=%u\n",
               cases[i].level, cases[i].rawDamage, cases[i].defense, actual);
    }
    if (vm_net_mock_pve_damage_after_defense(1000u, UINT32_MAX, 70u) != 150u)
    {
        fputs("PvE 85 percent mitigation ceiling changed\n", stderr);
        return 1;
    }
    return 0;
}

static int assert_expected_profile_and_monster_targets(void)
{
    vm_net_mock_monster_level_reference previous = {0};
    vm_net_mock_monster_level_reference reference = {0};
    vm_net_mock_monster_entry normalEntry = {70001u, 70u,
                                              VM_NET_MOCK_MONSTER_BEAST, 0, 0};
    vm_net_mock_monster_entry bossEntry = {70002u, 70u,
                                            VM_NET_MOCK_MONSTER_BOSS, 0, 0};
    vm_net_mock_monster_stats normal;
    vm_net_mock_monster_stats boss;
    u32 damage = 0;

    for (u32 level = 1; level <= VM_NET_MOCK_ROLE_LEVEL_CAP; ++level)
    {
        vm_net_mock_monster_pve_expected_reference_for_level(level, &reference);
        if (reference.hp < previous.hp || reference.mp < previous.mp ||
            reference.attack < previous.attack ||
            reference.defense < previous.defense ||
            reference.resist < previous.resist)
        {
            fprintf(stderr, "PvE expected profile regressed at level %u\n", level);
            return 1;
        }
        previous = reference;
    }
    normal = vm_net_mock_monster_base_stats_for_entry_curve(
        &normalEntry, VM_NET_MOCK_MONSTER_BALANCE_CURVE_V7);
    damage = vm_net_mock_pve_damage_after_defense(
        reference.attack, normal.defense, 70u);
    if ((normal.hp + damage - 1u) / damage != 7u)
    {
        fprintf(stderr, "normal level-70 target changed hp=%u outgoing=%u\n",
                normal.hp, damage);
        return 1;
    }
    damage = vm_net_mock_pve_damage_after_defense(
        normal.attack, reference.defense, 70u);
    if ((reference.hp + damage - 1u) / damage != 16u)
    {
        fprintf(stderr, "normal level-70 survival target changed hp=%u incoming=%u\n",
                reference.hp, damage);
        return 1;
    }
    boss = vm_net_mock_monster_base_stats_for_entry_curve(
        &bossEntry, VM_NET_MOCK_MONSTER_BALANCE_CURVE_V7);
    damage = vm_net_mock_pve_damage_after_defense(
        reference.attack, boss.defense, 70u);
    if ((boss.hp + damage - 1u) / damage != 36u)
    {
        fprintf(stderr, "boss level-70 target changed hp=%u outgoing=%u\n",
                boss.hp, damage);
        return 1;
    }
    damage = vm_net_mock_pve_damage_after_defense(
        boss.attack, reference.defense, 70u);
    if ((reference.hp + damage - 1u) / damage != 9u)
    {
        fprintf(stderr, "boss level-70 survival target changed hp=%u incoming=%u\n",
                reference.hp, damage);
        return 1;
    }
    return 0;
}

static int assert_magic_bypasses_armour(void)
{
    u32 noArmour = vm_net_mock_battle_enemy_damage_after_pve_mitigation(
        1000u, 0u, 1000u, 70u, true);
    u32 highArmour = vm_net_mock_battle_enemy_damage_after_pve_mitigation(
        1000u, 100000u, 1000u, 70u, true);
    u32 physical = vm_net_mock_battle_enemy_damage_after_pve_mitigation(
        1000u, 100000u, 1000u, 70u, false);

    if (noArmour != 500u || highArmour != 500u || physical >= noArmour)
    {
        fprintf(stderr,
                "magic armour boundary changed none=%u high=%u physical=%u\n",
                noArmour, highArmour, physical);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (assert_pve_damage_properties() != 0 ||
        assert_expected_profile_and_monster_targets() != 0 ||
        assert_magic_bypasses_armour() != 0)
    {
        return 1;
    }
    puts("pve defense mitigation regression passed");
    return 0;
}
