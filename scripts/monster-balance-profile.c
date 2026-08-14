/*
 * Deterministic profile for the server's monster balancing policy.
 *
 * It loads the shipped quality-0 equipment catalog and never opens a
 * listener, connects to MySQL, or changes role data.  The report compares
 * bare professions, their best same-level quality-0 outfits, and the
 * formula-derived beast/boss opponents at representative levels.  It is
 * deliberately concerned with turns-to-defeat / turns-to-be-defeated rather
 * than comparing raw values in isolation.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

static u32 ceil_div_u32(u32 value, u32 divisor)
{
    return divisor == 0 ? 0 : (value + divisor - 1u) / divisor;
}

static void make_unarmed_role(vm_net_mock_role_state *role, u32 level, u32 job)
{
    memset(role, 0, sizeof(*role));
    role->roleId = 900000u + level * 10u + job;
    role->level = level;
    role->exp = vm_net_mock_role_level_start_exp(level);
    role->job = (u8)job;
}

static void print_matchup(const char *label,
                          const vm_net_mock_player_stats *player,
                          const vm_net_mock_monster_stats *monster)
{
    u32 playerDamage = vm_net_mock_damage_after_defense(player->attack,
                                                         monster->defense);
    u32 enemyDamage = vm_net_mock_damage_after_defense(monster->attack,
                                                        player->defense);

    printf("  %-5s role=%4u/%4u/%4u/%4u monster=%5u/%4u/%5u/%4u "
           "turns=kill:%2u survive:%2u\n",
           label, player->maxHp, player->maxMp, player->attack, player->defense,
           monster->hp, monster->mp, monster->attack, monster->defense,
           ceil_div_u32(monster->hp, playerDamage),
           ceil_div_u32(player->maxHp, enemyDamage));
}

static bool check_curve_sample(u32 level,
                               const vm_net_mock_monster_level_reference *reference,
                               const vm_net_mock_monster_stats *normal,
                               const vm_net_mock_monster_stats *boss)
{
    if (level == 1u &&
        (reference->hp != 120u || reference->mp != 100u ||
         reference->attack != 26u || reference->defense != 8u ||
         normal->hp != 144u || normal->mp != 20u || normal->attack != 10u ||
         normal->defense != 6u || boss->hp != 690u || boss->mp != 50u ||
         boss->attack != 19u || boss->defense != 13u))
        return false;
    if (level == 60u &&
        (reference->hp != 2234u || reference->mp != 1555u ||
         reference->attack != 425u || reference->defense != 3588u ||
         normal->hp != 1374u || normal->mp != 311u || normal->attack != 5885u ||
         normal->defense != 85u || boss->hp != 4080u || boss->mp != 778u ||
         boss->attack != 11770u || boss->defense != 213u))
        return false;
    if (level == 70u &&
        (reference->hp != 3496u || reference->mp != 2253u ||
         reference->attack != 791u || reference->defense != 5026u ||
         normal->hp != 1830u || normal->mp != 451u || normal->attack != 12801u ||
         normal->defense != 159u || boss->hp != 4770u || boss->mp != 1127u ||
         boss->attack != 25601u || boss->defense != 396u))
        return false;
    return true;
}

int main(void)
{
    static const u32 levels[] = {1u, 10u, 20u, 30u, 40u, 50u, 60u, 70u};

    for (u32 i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i)
    {
        vm_net_mock_monster_entry normalEntry;
        vm_net_mock_monster_entry bossEntry;
        vm_net_mock_monster_stats normal;
        vm_net_mock_monster_stats boss;
        vm_net_mock_monster_level_reference reference;

        memset(&normalEntry, 0, sizeof(normalEntry));
        normalEntry.enemyId = 65000u + levels[i];
        normalEntry.level = (u8)levels[i];
        normalEntry.family = VM_NET_MOCK_MONSTER_BEAST;
        bossEntry = normalEntry;
        bossEntry.enemyId += 100u;
        bossEntry.family = VM_NET_MOCK_MONSTER_BOSS;
        normal = vm_net_mock_monster_base_stats_for_entry(&normalEntry);
        boss = vm_net_mock_monster_base_stats_for_entry(&bossEntry);
        vm_net_mock_monster_level_reference_for_level(levels[i], &reference);
        if (!check_curve_sample(levels[i], &reference, &normal, &boss))
        {
            fprintf(stderr, "unexpected quality-0 monster curve at level %u\n",
                    levels[i]);
            return 1;
        }

        printf("level %u\n", levels[i]);
        for (u32 job = 1; job <= 3; ++job)
        {
            vm_net_mock_role_state bareRole;
            vm_net_mock_player_stats bare;
            vm_net_mock_player_stats qualityZero;

            make_unarmed_role(&bareRole, levels[i], job);
            vm_net_mock_role_build_base_player_stats(&bareRole, &bare);
            if (!vm_net_mock_monster_quality_zero_reference_for_job(
                    levels[i], job, &qualityZero))
            {
                fputs("quality-0 reference build failed\n", stderr);
                return 1;
            }
            printf(" job%u bare=%4u/%4u/%4u/%4u q0=%4u/%4u/%4u/%4u\n",
                   job, bare.maxHp, bare.maxMp, bare.attack, bare.defense,
                   qualityZero.maxHp, qualityZero.maxMp, qualityZero.attack,
                   qualityZero.defense);
            print_matchup("bare", &bare, &normal);
            print_matchup("normal", &qualityZero, &normal);
            print_matchup("boss", &qualityZero, &boss);
            if ((levels[i] == 60u || levels[i] == 70u) && job == 3u)
            {
                u32 normalDamage = vm_net_mock_damage_after_defense(
                    qualityZero.attack, normal.defense);
                u32 normalCounter = vm_net_mock_damage_after_defense(
                    normal.attack, qualityZero.defense);
                u32 bossDamage = vm_net_mock_damage_after_defense(
                    qualityZero.attack, boss.defense);
                u32 bossCounter = vm_net_mock_damage_after_defense(
                    boss.attack, qualityZero.defense);

                if (ceil_div_u32(normal.hp, normalDamage) != 6u ||
                    ceil_div_u32(qualityZero.maxHp, normalCounter) != 14u ||
                    ceil_div_u32(boss.hp, bossDamage) != 30u ||
                    ceil_div_u32(qualityZero.maxHp, bossCounter) != 8u)
                {
                    fprintf(stderr,
                            "equal-level turn-economy contract failed at level %u\n",
                            levels[i]);
                    return 1;
                }
            }
        }
    }
    puts("monster balance profile passed: quality-0 normal=6/14, boss=30/8 at levels 60 and 70");
    return 0;
}
