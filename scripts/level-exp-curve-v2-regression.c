/*
 * Pure regression for the V4 cumulative EXP curve.
 *
 * This test starts neither server nor client and opens no database.  It locks
 * down the player-visible thresholds, uniform monster PVE rewards, offline
 * practice proportion, cap behavior, and both legacy migration sources.
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
        {2, 6750}, {10, 87000}, {20, 467000}, {40, 9797000},
        {49, 44537000}, {50, 50072000}, {60, 222072000}, {70, 717747000}
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
    if (expect_u32("curve cap", vm_net_mock_role_exp_cap(), 717747000) ||
        expect_u32("level-70 interval",
                   vm_net_mock_role_exp_interval_for_level(70), 62235000))
    {
        return 1;
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

    if (expect_u32("normal level-4 reward",
                   vm_net_mock_normal_monster_exp_for_level(4), 3) ||
        expect_u32("normal level-5 reward",
                   vm_net_mock_normal_monster_exp_for_level(5), 5) ||
        expect_u32("normal level-10 reward",
                   vm_net_mock_normal_monster_exp_for_level(10), 8) ||
        expect_u32("normal level-40 reward",
                   vm_net_mock_normal_monster_exp_for_level(40), 90) ||
        expect_u32("normal level-60 reward",
                   vm_net_mock_normal_monster_exp_for_level(60), 450) ||
        expect_u32("normal level-70 reward",
                   vm_net_mock_normal_monster_exp_for_level(70), 675) ||
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
    if (expect_u32("default normal reward", normal.exp, 675) ||
        expect_u32("default boss reward", boss.exp, 675) ||
        expect_u32("default normal money", normal.gold, 168) ||
        expect_u32("default boss money", boss.gold, 168))
    {
        return 1;
    }

    /* A V7/V8 account remains level 49 and retains 37% of that interval. */
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
    if (!vm_net_mock_role_migrate_exp_curve_v3(&role, false, false, false, &oldLevel,
                                                &sourceCapped) ||
        oldLevel != 49 || sourceCapped || role.level != 49 ||
        role.exp != expectedExp)
    {
        fputs("V1 EXP migration did not preserve level progress\n", stderr);
        return 1;
    }

    /* A V9 V2-curve account retains the same level and 37% interval progress
     * when the banded V4 curve becomes live. */
    memset(&role, 0, sizeof(role));
    sourceStart = g_vm_net_mock_role_level_start_exp_v2[49];
    sourceInterval = g_vm_net_mock_role_level_start_exp_v2[50] - sourceStart;
    sourceProgress = (u32)(((uint64_t)sourceInterval * 37ull) / 100ull);
    role.exp = sourceStart + sourceProgress;
    newStart = vm_net_mock_role_level_start_exp(49);
    newInterval = vm_net_mock_role_exp_interval_for_level(49);
    expectedExp = newStart + (u32)(((uint64_t)sourceProgress * newInterval) /
                                    sourceInterval);
    if (!vm_net_mock_role_migrate_exp_curve_v3(&role, false, true, false, &oldLevel,
                                                &sourceCapped) ||
        oldLevel != 49 || sourceCapped || role.level != 49 ||
        role.exp != expectedExp)
    {
        fputs("V2 EXP migration did not preserve level progress\n", stderr);
        return 1;
    }

    /* The deployed V10 curve is the direct source of this release. */
    memset(&role, 0, sizeof(role));
    sourceStart = g_vm_net_mock_role_level_start_exp_v3[49];
    sourceInterval = g_vm_net_mock_role_level_start_exp_v3[50] - sourceStart;
    sourceProgress = (u32)(((uint64_t)sourceInterval * 37ull) / 100ull);
    role.exp = sourceStart + sourceProgress;
    newStart = vm_net_mock_role_level_start_exp(49);
    newInterval = vm_net_mock_role_exp_interval_for_level(49);
    expectedExp = newStart + (u32)(((uint64_t)sourceProgress * newInterval) /
                                    sourceInterval);
    if (!vm_net_mock_role_migrate_exp_curve_v3(&role, false, false, true, &oldLevel,
                                                &sourceCapped) ||
        oldLevel != 49 || sourceCapped || role.level != 49 ||
        role.exp != expectedExp)
    {
        fputs("V3 EXP migration did not preserve level progress\n", stderr);
        return 1;
    }

    /* V6 and older use the documented quadratic/linear-increment source. */
    memset(&role, 0, sizeof(role));
    sourceStart = vm_net_mock_role_legacy_level_start_exp(40);
    sourceInterval = vm_net_mock_role_legacy_level_start_exp(41) - sourceStart;
    sourceProgress = (u32)(((uint64_t)sourceInterval * 2ull) / 3ull);
    role.exp = sourceStart + sourceProgress;
    newStart = vm_net_mock_role_level_start_exp(40);
    newInterval = vm_net_mock_role_exp_interval_for_level(40);
    expectedExp = newStart + (u32)(((uint64_t)sourceProgress * newInterval) /
                                    sourceInterval);
    if (!vm_net_mock_role_migrate_exp_curve_v3(&role, true, false, false, &oldLevel,
                                                &sourceCapped) ||
        oldLevel != 40 || sourceCapped || role.level != 40 ||
        role.exp != expectedExp)
    {
        fputs("legacy EXP migration did not preserve level progress\n", stderr);
        return 1;
    }

    memset(&role, 0, sizeof(role));
    role.exp = g_vm_net_mock_role_level_start_exp_v1[70];
    if (!vm_net_mock_role_migrate_exp_curve_v3(&role, false, false, false, &oldLevel,
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

    {
        u32 requiredFights = 0;

        for (u32 level = 1; level < VM_NET_MOCK_ROLE_LEVEL_CAP; ++level)
        {
            u32 interval = vm_net_mock_role_exp_interval_for_level(level);
            u32 reward = vm_net_mock_normal_monster_exp_for_level(level) * 10u;

            requiredFights += (interval + reward - 1u) / reward;
        }
        if (expect_u32("1-70 10x normal kills", requiredFights, 216000) ||
            expect_u32("daily monster reward quota",
                       VM_NET_MOCK_MONSTER_REWARD_DAILY_UNIT_CAP, 7200))
        {
            return 1;
        }
    }

    normalRate = vm_net_mock_practise_exp_per_minute(
        vm_net_mock_role_level_start_exp(60), false);
    goldRate = vm_net_mock_practise_exp_per_minute(
        vm_net_mock_role_level_start_exp(60), true);
    if (expect_u32("normal practice level-60 rate", normalRate, 1538) ||
        expect_u32("gold practice level-60 rate", goldRate, 3076) ||
        expect_u32("normal practice level-60 daily output", normalRate * 480u,
                   738240))
    {
        return 1;
    }
    {
        vm_net_mock_task_definition task;

        memset(&task, 0, sizeof(task));
        task.level = 20;
        task.rewardExp = 42000;
        if (expect_u32("level-20 task reward cap",
                       vm_net_mock_task_effective_reward_exp(&task), 8064))
        {
            return 1;
        }
    }

    puts("level-exp-curve-v4 regression passed: thresholds, rewards, migration, cap, and practice rate");
    return 0;
}
