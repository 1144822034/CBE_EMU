/*
 * Pure regression for the V16 MMORPG tiered cumulative EXP curve.
 *
 * This test starts neither server nor client and opens no database.  It locks
 * down the player-visible thresholds, the reduced late-game normal-monster
 * rewards, offline practice proportion, cap behavior, and every retained
 * migration source.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

static int expect_u32(const char *name, u32 actual, u32 expected)
{
    if (actual == expected)
        return 0;
    fprintf(stderr, "%s: got %u, expected %u\n", name, actual, expected);
    return 1;
}

int main(void)
{
    static const struct
    {
        u32 level;
        u32 startExp;
    } anchors[] = {
        {2, 120}, {3, 316}, {4, 604}, {5, 1000}, {6, 1520},
        {10, 4780}, {20, 23990}, {40, 467400},
        {49, 4383400}, {50, 5423400}, {60, 61260900}, {70, 562640900}
    };
    static const u32 expectedSameLevelKills[
        VM_NET_MOCK_ROLE_LEVEL_CAP + 1] = {
        0, 12, 14, 16, 18, 20, 21, 22, 23, 24,
        25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
        35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
        90, 100, 110, 120, 130, 180, 200, 220, 240, 260,
        360, 400, 440, 480, 520, 720, 800, 880, 960, 1040,
        1400, 1550, 1700, 1850, 2000, 2600, 2900, 3200, 3500, 3800,
        4600, 5100, 5600, 6100, 6600, 7800, 8600, 9400, 10200, 11000,
        0
    };
    static const u32 expectedLateMonsterExp[] = {
        80, 86, 92, 98, 104, 120, 132, 144, 156, 168,
        210, 231, 252, 273, 294, 350, 385, 420, 455, 490,
        600, 660, 720, 780, 840, 1050, 1155, 1260, 1365, 1470,
        1800, 1980, 2160, 2340, 2520, 3000, 3300, 3600, 3900, 4200,
        4200
    };
    vm_net_mock_role_state role;
    vm_net_mock_monster_entry entry;
    vm_net_mock_monster_stats normal;
    vm_net_mock_monster_stats boss;
    u32 oldLevel = 0;
    bool sourceCapped = false;
    u32 sourceStart = 0;
    u32 sourceInterval = 0;
    u32 sourceProgress = 0;
    u32 newStart = 0;
    u32 newInterval = 0;
    u32 expectedExp = 0;
    u32 normalRate = 0;
    u32 goldRate = 0;

    for (u32 i = 0; i < sizeof(anchors) / sizeof(anchors[0]); ++i)
    {
        if (expect_u32("curve anchor",
                       vm_net_mock_role_level_start_exp(anchors[i].level),
                       anchors[i].startExp))
        {
            return 1;
        }
    }
    if (expect_u32("curve cap", vm_net_mock_role_exp_cap(), 562640900) ||
        expect_u32("level-70 interval",
                   vm_net_mock_role_exp_interval_for_level(70), 100100000))
    {
        return 1;
    }
    {
        u32 previousInterval = 0;

        for (u32 level = 1; level < VM_NET_MOCK_ROLE_LEVEL_CAP; ++level)
        {
            u32 interval = vm_net_mock_role_exp_interval_for_level(level);

            if (interval == 0 || (previousInterval != 0 &&
                                  interval <= previousInterval))
            {
                fprintf(stderr, "level interval is not strictly increasing at %u\n",
                        level);
                return 1;
            }
            previousInterval = interval;
        }
    }
    for (u32 level = 1; level < VM_NET_MOCK_ROLE_LEVEL_CAP; ++level)
    {
        u32 start = vm_net_mock_role_level_start_exp(level);
        u32 next = vm_net_mock_role_level_start_exp(level + 1);

        if (vm_net_mock_role_level_from_exp(start) != level ||
            vm_net_mock_role_level_from_exp(next - 1u) != level ||
            vm_net_mock_role_level_from_exp(next) != level + 1u)
        {
            fprintf(stderr, "level boundary failed at %u\n", level);
            return 1;
        }
    }

    if (expect_u32("normal level-1 reward",
                   vm_net_mock_normal_monster_exp_for_level(1), 10) ||
        expect_u32("normal level-4 reward",
                   vm_net_mock_normal_monster_exp_for_level(4), 22) ||
        expect_u32("normal level-5 reward",
                   vm_net_mock_normal_monster_exp_for_level(5), 26) ||
        expect_u32("normal level-10 reward",
                   vm_net_mock_normal_monster_exp_for_level(10), 46) ||
        expect_u32("normal level-40 reward",
                   vm_net_mock_normal_monster_exp_for_level(40), 210) ||
        expect_u32("normal level-60 reward",
                   vm_net_mock_normal_monster_exp_for_level(60), 1800) ||
        expect_u32("normal level-70 reward",
                   vm_net_mock_normal_monster_exp_for_level(70), 4200) ||
        expect_u32("normal level-70 money",
                   vm_net_mock_normal_monster_gold_for_level(70), 168))
    {
        return 1;
    }
    if (expect_u32("2x card", vm_net_mock_exp_card_multiplier_for_item(809), 2) ||
        expect_u32("4x card", vm_net_mock_exp_card_multiplier_for_item(810), 4) ||
        expect_u32("10x card", vm_net_mock_exp_card_multiplier_for_item(811), 10))
    {
        return 1;
    }
    memset(&entry, 0, sizeof(entry));
    entry.level = 70;
    entry.family = VM_NET_MOCK_MONSTER_BEAST;
    normal = vm_net_mock_monster_base_stats_for_entry(&entry);
    entry.family = VM_NET_MOCK_MONSTER_BOSS;
    boss = vm_net_mock_monster_base_stats_for_entry(&entry);
    if (expect_u32("default normal reward", normal.exp, 4200) ||
        expect_u32("default boss reward", boss.exp, 21000) ||
        expect_u32("default normal money", normal.gold, 168) ||
        expect_u32("default boss money", boss.gold, 168))
    {
        return 1;
    }

    /* A V7 account remains level 49 and retains 37% of that interval. */
    memset(&role, 0, sizeof(role));
    sourceStart = g_vm_net_mock_role_level_start_exp_v1[49];
    sourceInterval = g_vm_net_mock_role_level_start_exp_v1[50] - sourceStart;
    sourceProgress = (u32)(((uint64_t)sourceInterval * 37ull) / 100ull);
    role.exp = sourceStart + sourceProgress;
    role.level = 1; /* EXP, not this stale field, is the migration authority. */
    newStart = vm_net_mock_role_level_start_exp(49);
    newInterval = vm_net_mock_role_exp_interval_for_level(49);
    expectedExp = newStart + (u32)(((uint64_t)sourceProgress * newInterval) /
                                    sourceInterval);
    if (!vm_net_mock_role_migrate_exp_curve(&role, false, false, false, false, false, false, false, false, &oldLevel,
                                                &sourceCapped) ||
        oldLevel != 49 || sourceCapped || role.level != 49 ||
        role.exp != expectedExp)
    {
        fputs("V1 EXP migration did not preserve level progress\n", stderr);
        return 1;
    }

    /* A V9 V2-curve account retains the same level and 37% interval progress
     * when the tiered curve becomes live. */
    memset(&role, 0, sizeof(role));
    sourceStart = g_vm_net_mock_role_level_start_exp_v2[49];
    sourceInterval = g_vm_net_mock_role_level_start_exp_v2[50] - sourceStart;
    sourceProgress = (u32)(((uint64_t)sourceInterval * 37ull) / 100ull);
    role.exp = sourceStart + sourceProgress;
    newStart = vm_net_mock_role_level_start_exp(49);
    newInterval = vm_net_mock_role_exp_interval_for_level(49);
    expectedExp = newStart + (u32)(((uint64_t)sourceProgress * newInterval) /
                                    sourceInterval);
    if (!vm_net_mock_role_migrate_exp_curve(&role, false, true, false, false, false, false, false, false, &oldLevel,
                                                &sourceCapped) ||
        oldLevel != 49 || sourceCapped || role.level != 49 ||
        role.exp != expectedExp)
    {
        fputs("V2 EXP migration did not preserve level progress\n", stderr);
        return 1;
    }

    /* The deployed V10 curve retains its level and progress in V16. */
    memset(&role, 0, sizeof(role));
    sourceStart = g_vm_net_mock_role_level_start_exp_v3[49];
    sourceInterval = g_vm_net_mock_role_level_start_exp_v3[50] - sourceStart;
    sourceProgress = (u32)(((uint64_t)sourceInterval * 37ull) / 100ull);
    role.exp = sourceStart + sourceProgress;
    newStart = vm_net_mock_role_level_start_exp(49);
    newInterval = vm_net_mock_role_exp_interval_for_level(49);
    expectedExp = newStart + (u32)(((uint64_t)sourceProgress * newInterval) /
                                    sourceInterval);
    if (!vm_net_mock_role_migrate_exp_curve(&role, false, false, true, false, false, false, false, false, &oldLevel,
                                                &sourceCapped) ||
        oldLevel != 49 || sourceCapped || role.level != 49 ||
        role.exp != expectedExp)
    {
        fputs("V3 EXP migration did not preserve level progress\n", stderr);
        return 1;
    }

    /* A deployed V11 role keeps its level and 37% interval progress. */
    memset(&role, 0, sizeof(role));
    sourceStart = g_vm_net_mock_role_level_start_exp_v4[49];
    sourceInterval = g_vm_net_mock_role_level_start_exp_v4[50] - sourceStart;
    sourceProgress = (u32)(((uint64_t)sourceInterval * 37ull) / 100ull);
    role.exp = sourceStart + sourceProgress;
    newStart = vm_net_mock_role_level_start_exp(49);
    newInterval = vm_net_mock_role_exp_interval_for_level(49);
    expectedExp = newStart + (u32)(((uint64_t)sourceProgress * newInterval) /
                                    sourceInterval);
    if (!vm_net_mock_role_migrate_exp_curve(&role, false, false, false, true, false, false, false, false,
                                                &oldLevel, &sourceCapped) ||
        oldLevel != 49 || sourceCapped || role.level != 49 ||
        role.exp != expectedExp)
    {
        fputs("deployed V11 EXP migration did not preserve level progress\n", stderr);
        return 1;
    }

    /* A deployed V12 early-ramp role follows the same preserve-level-progress
     * contract. */
    memset(&role, 0, sizeof(role));
    sourceStart = g_vm_net_mock_role_level_start_exp_v5[49];
    sourceInterval = g_vm_net_mock_role_level_start_exp_v5[50] - sourceStart;
    sourceProgress = (u32)(((uint64_t)sourceInterval * 37ull) / 100ull);
    role.exp = sourceStart + sourceProgress;
    newStart = vm_net_mock_role_level_start_exp(49);
    newInterval = vm_net_mock_role_exp_interval_for_level(49);
    expectedExp = newStart + (u32)(((uint64_t)sourceProgress * newInterval) /
                                    sourceInterval);
    if (!vm_net_mock_role_migrate_exp_curve(&role, false, false, false, false, true, false, false, false,
                                             &oldLevel, &sourceCapped) ||
        oldLevel != 49 || sourceCapped || role.level != 49 ||
        role.exp != expectedExp)
    {
        fputs("deployed V12 EXP migration did not preserve level progress\n", stderr);
        return 1;
    }

    /* A deployed V13 role retains its level and in-level percentage. */
    memset(&role, 0, sizeof(role));
    sourceStart = g_vm_net_mock_role_level_start_exp_v6[49];
    sourceInterval = g_vm_net_mock_role_level_start_exp_v6[50] - sourceStart;
    sourceProgress = (u32)(((uint64_t)sourceInterval * 37ull) / 100ull);
    role.exp = sourceStart + sourceProgress;
    newStart = vm_net_mock_role_level_start_exp(49);
    newInterval = vm_net_mock_role_exp_interval_for_level(49);
    expectedExp = newStart + (u32)(((uint64_t)sourceProgress * newInterval) /
                                    sourceInterval);
    if (!vm_net_mock_role_migrate_exp_curve(&role, false, false, false, false, false, true, false, false,
                                             &oldLevel, &sourceCapped) ||
        oldLevel != 49 || sourceCapped || role.level != 49 ||
        role.exp != expectedExp)
    {
        fputs("deployed V13 EXP migration did not preserve level progress\n", stderr);
        return 1;
    }

    /* A deployed V14 role retains its level and in-level percentage. */
    memset(&role, 0, sizeof(role));
    sourceStart = g_vm_net_mock_role_level_start_exp_v7[49];
    sourceInterval = g_vm_net_mock_role_level_start_exp_v7[50] - sourceStart;
    sourceProgress = (u32)(((uint64_t)sourceInterval * 37ull) / 100ull);
    role.exp = sourceStart + sourceProgress;
    newStart = vm_net_mock_role_level_start_exp(49);
    newInterval = vm_net_mock_role_exp_interval_for_level(49);
    expectedExp = newStart + (u32)(((uint64_t)sourceProgress * newInterval) /
                                    sourceInterval);
    if (!vm_net_mock_role_migrate_exp_curve(&role, false, false, false, false, false, false, true, false,
                                             &oldLevel, &sourceCapped) ||
        oldLevel != 49 || sourceCapped || role.level != 49 ||
        role.exp != expectedExp)
    {
        fputs("deployed V14 EXP migration did not preserve level progress\n", stderr);
        return 1;
    }

    /* V15 used the original V3 percentage curve.  It too keeps the same
     * level and in-level percentage when V16 becomes live. */
    memset(&role, 0, sizeof(role));
    sourceStart = g_vm_net_mock_role_level_start_exp_v3[49];
    sourceInterval = g_vm_net_mock_role_level_start_exp_v3[50] - sourceStart;
    sourceProgress = (u32)(((uint64_t)sourceInterval * 37ull) / 100ull);
    role.exp = sourceStart + sourceProgress;
    newStart = vm_net_mock_role_level_start_exp(49);
    newInterval = vm_net_mock_role_exp_interval_for_level(49);
    expectedExp = newStart + (u32)(((uint64_t)sourceProgress * newInterval) /
                                    sourceInterval);
    if (!vm_net_mock_role_migrate_exp_curve(&role, false, false, false, false, false, false, false, true,
                                             &oldLevel, &sourceCapped) ||
        oldLevel != 49 || sourceCapped || role.level != 49 ||
        role.exp != expectedExp)
    {
        fputs("deployed V15 EXP migration did not preserve level progress\n", stderr);
        return 1;
    }

    /* Pre-V7 rows use the documented quadratic/linear-increment source. */
    memset(&role, 0, sizeof(role));
    sourceStart = vm_net_mock_role_legacy_level_start_exp(40);
    sourceInterval = vm_net_mock_role_legacy_level_start_exp(41) - sourceStart;
    sourceProgress = (u32)(((uint64_t)sourceInterval * 2ull) / 3ull);
    role.exp = sourceStart + sourceProgress;
    newStart = vm_net_mock_role_level_start_exp(40);
    newInterval = vm_net_mock_role_exp_interval_for_level(40);
    expectedExp = newStart + (u32)(((uint64_t)sourceProgress * newInterval) /
                                    sourceInterval);
    if (!vm_net_mock_role_migrate_exp_curve(&role, true, false, false, false, false, false, false, false, &oldLevel,
                                                &sourceCapped) ||
        oldLevel != 40 || sourceCapped || role.level != 40 ||
        role.exp != expectedExp)
    {
        fputs("legacy EXP migration did not preserve level progress\n", stderr);
        return 1;
    }

    memset(&role, 0, sizeof(role));
    role.exp = g_vm_net_mock_role_level_start_exp_v1[70];
    if (!vm_net_mock_role_migrate_exp_curve(&role, false, false, false, false, false, false, false, false, &oldLevel,
                                                &sourceCapped) ||
        oldLevel != 70 || !sourceCapped ||
        role.level != VM_NET_MOCK_ROLE_LEVEL_CAP ||
        role.exp != vm_net_mock_role_exp_cap())
    {
        fputs("capped V1 EXP migration failed\n", stderr);
        return 1;
    }

    memset(&role, 0, sizeof(role));
    role.level = 69;
    role.exp = vm_net_mock_role_level_start_exp(69);
    if (!vm_net_mock_role_add_exp(
            &role, vm_net_mock_role_exp_cap() - role.exp) ||
        role.level != 70 || role.exp != vm_net_mock_role_exp_cap() ||
        vm_net_mock_role_add_exp(&role, 1))
    {
        fputs("level-cap settlement failed\n", stderr);
        return 1;
    }

    for (u32 level = 1; level < 30u; ++level)
    {
        u32 interval = vm_net_mock_role_exp_interval_for_level(level);
        u32 normalReward = vm_net_mock_normal_monster_exp_for_level(level);

        if ((uint64_t)normalReward * expectedSameLevelKills[level] != interval)
        {
            fprintf(stderr,
                    "same-level reward contract failed at %u: reward=%u kills=%u interval=%u\n",
                    level, normalReward, expectedSameLevelKills[level], interval);
            return 1;
        }
    }
    for (u32 level = 30; level <= VM_NET_MOCK_ROLE_LEVEL_CAP; ++level)
    {
        if (expect_u32("reduced late monster reward",
                       vm_net_mock_normal_monster_exp_for_level(level),
                       expectedLateMonsterExp[level - 30u]))
        {
            return 1;
        }
    }
    if (expect_u32("V16 late monster predecessor",
                   vm_net_mock_normal_monster_exp_v9_predecessor(40), 420) ||
        expect_u32("V16 cap-level monster predecessor",
                   vm_net_mock_normal_monster_exp_v9_predecessor(70), 9100))
    {
        return 1;
    }

    normalRate = vm_net_mock_practise_exp_per_minute(
        vm_net_mock_role_level_start_exp(60), false);
    goldRate = vm_net_mock_practise_exp_per_minute(
        vm_net_mock_role_level_start_exp(60), true);
    if (expect_u32("normal practice level-60 rate", normalRate, 729) ||
        expect_u32("gold practice level-60 rate", goldRate, 1458) ||
        expect_u32("normal practice level-60 daily output", normalRate * 480u,
                   349920))
    {
        return 1;
    }
    {
        vm_net_mock_task_definition task;

        memset(&task, 0, sizeof(task));
        task.level = 20;
        task.rewardExp = 42000;
        if (expect_u32("level-20 task reward cap",
                       vm_net_mock_task_effective_reward_exp(&task), 241))
        {
            return 1;
        }
    }

    if (expect_u32("normal role-level reward cap",
                   vm_net_mock_battle_base_exp_cap_for_role_level(
                       40, VM_NET_MOCK_MONSTER_BEAST),
                   210) ||
        expect_u32("boss role-level reward cap",
                   vm_net_mock_battle_base_exp_cap_for_role_level(
                       40, VM_NET_MOCK_MONSTER_BOSS),
                   1050) ||
        expect_u32("tenfold card after normal reward cap",
                   vm_net_mock_mul_capped_u32(
                       vm_net_mock_battle_base_exp_cap_for_role_level(
                           40, VM_NET_MOCK_MONSTER_BEAST),
                       10),
                   2100))
    {
        return 1;
    }
    puts("level-exp-curve-v9 regression passed: thresholds, reduced late rewards, migration, cap, and practice rate");
    return 0;
}
