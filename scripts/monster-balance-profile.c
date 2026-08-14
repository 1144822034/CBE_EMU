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
         normal->hp != 40u || normal->mp != 20u || normal->attack != 13u ||
         normal->defense != 3u || boss->hp != 120u || boss->mp != 50u ||
         boss->attack != 26u || boss->defense != 4u))
        return false;
    if (level == 60u &&
        (reference->hp != 2234u || reference->mp != 1555u ||
         reference->attack != 425u || reference->defense != 3588u ||
         normal->hp != 1561u || normal->mp != 300u || normal->attack != 4704u ||
         normal->defense != 81u || boss->hp != 4576u || boss->mp != 749u ||
         boss->attack != 7839u || boss->defense != 182u))
        return false;
    if (level == 70u &&
        (reference->hp != 3496u || reference->mp != 2253u ||
         reference->attack != 791u || reference->defense != 5026u ||
         normal->hp != 2135u || normal->mp != 451u || normal->attack != 11201u ||
         normal->defense != 159u || boss->hp != 6228u || boss->mp != 1127u ||
         boss->attack != 19912u || boss->defense != 356u))
        return false;
    return true;
}

int main(void)
{
    static const u32 levels[] = {1u, 3u, 10u, 20u, 30u, 40u, 50u, 60u, 70u};

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
        if (levels[i] == 3u)
        {
            vm_net_mock_monster_entry slimeEntry = normalEntry;
            vm_net_mock_monster_stats slime;

            slimeEntry.enemyId = VM_NET_MOCK_BATTLE_POISON_SLIME_ID;
            slimeEntry.family = VM_NET_MOCK_MONSTER_SLIME;
            slime = vm_net_mock_monster_base_stats_for_entry(&slimeEntry);
            if (slime.hp != 58u || slime.mp != 29u || slime.attack != 15u ||
                slime.defense != 3u)
            {
                fputs("unexpected level-3 toxic-slime tutorial profile\n", stderr);
                return 1;
            }
        }
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

                u32 expectedNormalSurvive = levels[i] == 60u ? 18u : 16u;
                u32 expectedBossKill = levels[i] == 60u ? 31u : 36u;
                u32 expectedBossSurvive = levels[i] == 60u ? 11u : 10u;

                if (ceil_div_u32(normal.hp, normalDamage) != 7u ||
                    ceil_div_u32(qualityZero.maxHp, normalCounter) !=
                        expectedNormalSurvive ||
                    ceil_div_u32(boss.hp, bossDamage) != expectedBossKill ||
                    ceil_div_u32(qualityZero.maxHp, bossCounter) !=
                        expectedBossSurvive)
                {
                    fprintf(stderr,
                            "equal-level turn-economy contract failed at level %u\n",
                            levels[i]);
                    return 1;
                }
            }
        }
    }
    puts("monster balance profile passed: stage-1 bare tutorial plus equipped level-60/70 contracts");
    return 0;
}
