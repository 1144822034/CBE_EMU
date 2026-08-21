/*
 * Read-only audit of the shipped task.dsh and automatic monster catalog.
 * It deliberately does not open MySQL, load player state, or start a server.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

typedef struct
{
    u32 count;
    uint64_t total;
    u32 minimum;
    u32 maximum;
} exp_audit_bucket;

static void exp_audit_add(exp_audit_bucket *bucket, u32 value)
{
    if (bucket == NULL)
        return;
    if (bucket->count == 0 || value < bucket->minimum)
        bucket->minimum = value;
    if (value > bucket->maximum)
        bucket->maximum = value;
    ++bucket->count;
    bucket->total += value;
}

static bool exp_audit_read_tasks(exp_audit_bucket buckets[7],
                                 uint64_t effectiveTotals[7],
                                 uint64_t effectiveByLevel[71],
                                 u32 *totalOut)
{
    char path[256];
    u8 data[32768];
    u32 len = 0;
    u32 columns = 0;
    u32 rows = 0;
    u32 headerBytes = 0;
    u32 pos = 16;
    u32 total = 0;

    if (buckets == NULL || effectiveTotals == NULL || effectiveByLevel == NULL ||
        !vm_net_mock_open_server_data_resource("task.dsh", ".dsh", NULL,
                                               path, sizeof(path)))
    {
        return false;
    }
    len = vm_net_mock_load_response_file(path, data, sizeof(data));
    if (len < 20 || vm_net_mock_read_le32_at(data, 0) != len - 4)
        return false;
    columns = vm_net_mock_read_le32_at(data, 4);
    rows = vm_net_mock_read_le32_at(data, 8);
    headerBytes = vm_net_mock_read_le32_at(data, 12);
    if (columns != 25 || rows == 0 || 16u + headerBytes > len)
        return false;
    for (u32 column = 0; column < columns; ++column)
    {
        u32 stringLen = 0;

        if (pos >= len)
            return false;
        stringLen = data[pos++];
        if (pos + stringLen > len)
            return false;
        pos += stringLen;
    }
    if (pos > 16u + headerBytes)
        return false;
    pos = 16u + headerBytes;
    for (u32 row = 0; row < rows && pos + 4 <= len; ++row)
    {
        const u8 *values[25];
        u8 valueLens[25];
        u32 rowLen = vm_net_mock_read_le32_at(data, pos);
        u32 rowEnd = 0;
        u32 level = 0;
        u32 rewardExp = 0;
        u32 bucket = 0;
        vm_net_mock_task_definition task;

        pos += 4;
        if (rowLen == 0 || rowLen > len - pos)
            return false;
        rowEnd = pos + rowLen;
        memset(values, 0, sizeof(values));
        memset(valueLens, 0, sizeof(valueLens));
        for (u32 column = 0; column < columns; ++column)
        {
            u32 stringLen = 0;

            if (pos >= rowEnd)
                return false;
            stringLen = data[pos++];
            if (pos + stringLen > rowEnd)
                return false;
            values[column] = data + pos;
            valueLens[column] = (u8)stringLen;
            pos += stringLen;
        }
        pos = rowEnd;
        level = vm_net_mock_parse_decimal_slice(values[2], valueLens[2]);
        rewardExp = vm_net_mock_parse_decimal_slice(values[11], valueLens[11]);
        bucket = level == 0 ? 0 : (level - 1u) / 10u;
        if (bucket > 6)
            bucket = 6;
        exp_audit_add(&buckets[bucket], rewardExp);
        memset(&task, 0, sizeof(task));
        task.level = (u8)level;
        task.rewardExp = rewardExp;
        rewardExp = vm_net_mock_task_effective_reward_exp(&task);
        effectiveTotals[bucket] += rewardExp;
        if (level > VM_NET_MOCK_ROLE_LEVEL_CAP)
            level = VM_NET_MOCK_ROLE_LEVEL_CAP;
        effectiveByLevel[level ? level : 1] += rewardExp;
        ++total;
    }
    if (totalOut)
        *totalOut = total;
    return total != 0;
}

/* This intentionally favourable model grants every shipped task as soon as its
 * required level is reached, then simulates 300 same-level normal victories,
 * a continuously active Battle Insight (+20% of the unmodified reward), and
 * both daily practice modes.  It is a cadence audit, not a live-server
 * shortcut or a replacement for task-completion conditions. */
static void exp_audit_add_capped(u32 *exp, uint64_t amount)
{
    u32 cap = vm_net_mock_role_exp_cap();

    if (exp == NULL || *exp >= cap)
        return;
    *exp = amount >= (uint64_t)cap - *exp ? cap : *exp + (u32)amount;
}

static void exp_audit_grant_available_tasks(u32 *exp,
                                            const uint64_t rewardsByLevel[71],
                                            bool granted[71])
{
    for (;;)
    {
        u32 level = exp == NULL ? VM_NET_MOCK_ROLE_LEVEL_CAP :
                    vm_net_mock_role_level_from_exp(*exp);

        if (level >= VM_NET_MOCK_ROLE_LEVEL_CAP || granted[level])
            return;
        granted[level] = true;
        exp_audit_add_capped(exp, rewardsByLevel[level]);
    }
}

static u32 exp_audit_days_with_all_tasks(const uint64_t rewardsByLevel[71],
                                         u32 monsterMultiplier,
                                         u32 battleInsightBonusPercent,
                                         bool includeBothPracticeModes)
{
    bool granted[71];
    u32 exp = 0;
    u32 days = 0;

    memset(granted, 0, sizeof(granted));
    while (exp < vm_net_mock_role_exp_cap())
    {
        ++days;
        exp_audit_grant_available_tasks(&exp, rewardsByLevel, granted);
        for (u32 victory = 0; victory < 300u &&
                              exp < vm_net_mock_role_exp_cap(); ++victory)
        {
            u32 level = vm_net_mock_role_level_from_exp(exp);
            u32 reward = vm_net_mock_normal_monster_exp_for_level(level);

            exp_audit_add_capped(
                &exp, (uint64_t)reward * monsterMultiplier +
                          ((uint64_t)reward * battleInsightBonusPercent) / 100u);
            exp_audit_grant_available_tasks(&exp, rewardsByLevel, granted);
        }
        if (includeBothPracticeModes && exp < vm_net_mock_role_exp_cap())
        {
            u32 normalRate = vm_net_mock_practise_exp_per_minute(exp, false);
            u32 goldRate = vm_net_mock_practise_exp_per_minute(exp, true);

            exp_audit_add_capped(&exp, (uint64_t)normalRate * 480u);
            exp_audit_add_capped(&exp, (uint64_t)goldRate * 240u);
            exp_audit_grant_available_tasks(&exp, rewardsByLevel, granted);
        }
    }
    return days;
}

int main(void)
{
    exp_audit_bucket taskBuckets[7];
    exp_audit_bucket monsterBuckets[7];
    uint64_t taskEffectiveTotals[7];
    uint64_t taskEffectiveByLevel[71];
    uint64_t normalKills = 0;
    uint64_t tenTimesKills = 0;
    uint64_t normalKillsWithTasks = 0;
    uint64_t tenTimesKillsWithTasks = 0;
    uint64_t normalExp = 0;
    uint64_t tenTimesExp = 0;
    u32 normalDaysWithPractice = 0;
    u32 tenTimesDaysWithPractice = 0;
    FILE *report = NULL;
    u32 taskTotal = 0;

    memset(taskBuckets, 0, sizeof(taskBuckets));
    memset(monsterBuckets, 0, sizeof(monsterBuckets));
    memset(taskEffectiveTotals, 0, sizeof(taskEffectiveTotals));
    memset(taskEffectiveByLevel, 0, sizeof(taskEffectiveByLevel));
    if (!exp_audit_read_tasks(taskBuckets, taskEffectiveTotals,
                              taskEffectiveByLevel, &taskTotal))
    {
        fputs("failed to parse task.dsh\n", stderr);
        return 1;
    }
    vm_net_mock_monster_catalog_ensure_loaded();
    for (u32 i = 0; i < g_vm_net_mock_monster_catalog_count; ++i)
    {
        const vm_net_mock_monster_entry *entry =
            &g_vm_net_mock_monster_catalog_entries[i];
        vm_net_mock_monster_stats stats =
            vm_net_mock_monster_base_stats_for_entry(entry);
        u32 level = entry->level ? entry->level : 1;
        u32 bucket = (level - 1u) / 10u;

        if (bucket > 6)
            bucket = 6;
        exp_audit_add(&monsterBuckets[bucket], stats.exp);
    }
    report = fopen("../tmp/exp-progression-audit.txt", "wb");
    if (report == NULL)
    {
        fputs("failed to create audit report\n", stderr);
        return 1;
    }
    fprintf(report, "task_rows=%u automatic_monster_rows=%u\n",
            taskTotal, g_vm_net_mock_monster_catalog_count);
    for (u32 level = 1; level < VM_NET_MOCK_ROLE_LEVEL_CAP; ++level)
    {
        u32 interval = vm_net_mock_role_exp_interval_for_level(level);
        u32 normalExp = vm_net_mock_normal_monster_exp_for_level(level);
        u32 tenTimesExp = normalExp > 0xffffffffu / 10u ?
                              0xffffffffu : normalExp * 10u;

        normalKills += (interval + normalExp - 1u) / normalExp;
        tenTimesKills += (interval + tenTimesExp - 1u) / tenTimesExp;
    }
    normalDaysWithPractice = exp_audit_days_with_all_tasks(
        taskEffectiveByLevel, 1u, 20u, true);
    tenTimesDaysWithPractice = exp_audit_days_with_all_tasks(
        taskEffectiveByLevel, 10u, 20u, true);
    for (u32 level = 1; level < VM_NET_MOCK_ROLE_LEVEL_CAP; ++level)
    {
        u32 next = vm_net_mock_role_level_start_exp(level + 1);
        u32 reward = vm_net_mock_normal_monster_exp_for_level(level);
        u32 tenTimesReward = reward > 0xffffffffu / 10u ?
                                 0xffffffffu : reward * 10u;

        normalExp += taskEffectiveByLevel[level];
        tenTimesExp += taskEffectiveByLevel[level];
        if (normalExp < next)
        {
            uint64_t needed = (uint64_t)next - normalExp;
            uint64_t kills = (needed + reward - 1u) / reward;

            normalKillsWithTasks += kills;
            normalExp += kills * reward;
        }
        if (tenTimesExp < next)
        {
            uint64_t needed = (uint64_t)next - tenTimesExp;
            uint64_t kills = (needed + tenTimesReward - 1u) / tenTimesReward;

            tenTimesKillsWithTasks += kills;
            tenTimesExp += kills * tenTimesReward;
        }
    }
    fprintf(report,
            "normal_same_level_kills_to_cap=%llu,ten_x_same_level_kills_to_cap=%llu,days_at_300_kills_per_day=%.2f/%.2f\n",
            (unsigned long long)normalKills,
            (unsigned long long)tenTimesKills,
            (double)normalKills / 300.0,
            (double)tenTimesKills / 300.0);
    fprintf(report,
            "same_level_kills_with_all_task_rewards=%llu/%llu,days_at_300_kills_per_day=%.2f/%.2f\n",
            (unsigned long long)normalKillsWithTasks,
            (unsigned long long)tenTimesKillsWithTasks,
            (double)normalKillsWithTasks / 300.0,
            (double)tenTimesKillsWithTasks / 300.0);
    fprintf(report,
            "all_tasks_immediate_300_wins_plus_battle_insight_plus_normal_and_gold_practice_days=%u/%u\n",
            normalDaysWithPractice, tenTimesDaysWithPractice);
    fprintf(report, "band,task_count,task_raw_total,task_effective_total,task_min,task_max,monster_count,monster_avg,monster_min,monster_max\n");
    for (u32 bucket = 0; bucket < 7; ++bucket)
    {
        uint64_t monsterAverage = monsterBuckets[bucket].count == 0 ? 0 :
            monsterBuckets[bucket].total / monsterBuckets[bucket].count;
        fprintf(report,
                "%u-%u,%u,%llu,%llu,%u,%u,%u,%llu,%u,%u\n",
                bucket * 10u + 1u, (bucket + 1u) * 10u,
                taskBuckets[bucket].count,
                (unsigned long long)taskBuckets[bucket].total,
                (unsigned long long)taskEffectiveTotals[bucket],
                taskBuckets[bucket].minimum, taskBuckets[bucket].maximum,
                monsterBuckets[bucket].count,
                (unsigned long long)monsterAverage,
                monsterBuckets[bucket].minimum, monsterBuckets[bucket].maximum);
    }
    fclose(report);
    return 0;
}
