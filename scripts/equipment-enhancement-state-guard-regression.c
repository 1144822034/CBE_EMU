/*
 * Regression for the equipment-enhancement persistence invariant.
 *
 * The production writer replaces equipment/backpack instance rows from one
 * projected role snapshot.  This fixture verifies the pure preflight rule:
 * a full snapshot may preserve or increase enhancement state, but it must not
 * silently reduce the number of enhanced instances or their aggregate level.
 */

#include <stdbool.h>
#include <stdio.h>

static bool enhancement_snapshot_is_non_regressive(
    unsigned persistedRows, unsigned persistedLevelSum,
    unsigned projectedRows, unsigned projectedLevelSum,
    bool roleDelete)
{
    if (roleDelete)
        return true;
    return projectedRows >= persistedRows &&
           projectedLevelSum >= persistedLevelSum;
}

int main(void)
{
    if (!enhancement_snapshot_is_non_regressive(2, 18, 2, 18, false) ||
        !enhancement_snapshot_is_non_regressive(2, 18, 3, 19, false) ||
        enhancement_snapshot_is_non_regressive(2, 18, 1, 18, false) ||
        enhancement_snapshot_is_non_regressive(2, 18, 2, 17, false) ||
        enhancement_snapshot_is_non_regressive(2, 18, 0, 0, false) ||
        !enhancement_snapshot_is_non_regressive(2, 18, 0, 0, true))
    {
        fputs("equipment enhancement state guard regression failed\n", stderr);
        return 1;
    }

    puts("equipment enhancement state guard regression passed: full snapshot downgrade is blocked and explicit role deletion remains allowed");
    return 0;
}
