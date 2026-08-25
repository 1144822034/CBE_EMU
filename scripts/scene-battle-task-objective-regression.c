/*
 * Deterministic scene-battle task-objective regression.
 *
 * This only exercises the task contract helpers in memory.  It does not open
 * a client, socket, or database connection, and does not mutate any role.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool expect_match(const vm_net_mock_task_definition *task, u8 slot,
                         u32 enemyId, const char *scene, bool expected,
                         const char *caseName)
{
    bool actual = vm_net_mock_task_battle_requirement_matches(
        task, slot, enemyId, scene);

    if (actual != expected)
    {
        fprintf(stderr,
                "%s failed: slot=%u enemy=%u scene=%s actual=%u expected=%u\n",
                caseName, slot, enemyId, scene ? scene : "-",
                actual ? 1u : 0u, expected ? 1u : 0u);
        return false;
    }
    return true;
}

int main(void)
{
    vm_net_mock_task_definition task;
    vm_net_mock_task_scene_battle_target_db_context targetContext;
    static const char knownScene[] =
        "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65";
    static const char knownSceneHex[] =
        "3030C5EEC0B3CFC9B5BA5F30322E736365";
    const char *targetValues[] = {"100000", "1", knownSceneHex};
    size_t targetLengths[] = {6, 1, sizeof(knownSceneHex) - 1};

    memset(&task, 0, sizeof(task));
    task.taskId = 100000;
    task.requirementType1 = 2;
    task.requirementId1 = 1001;
    task.requirementCount1 = 1;

    if (!vm_net_mock_task_scene_battle_targets_are_well_formed(&task) ||
        !expect_match(&task, 1, 1001, "other.sce", true,
                      "legacy-unmapped-kill-objective") ||
        !expect_match(&task, 1, 1002, "other.sce", false,
                      "legacy-wrong-monster"))
    {
        return 1;
    }

    snprintf(task.requirementScene1, sizeof(task.requirementScene1), "%s",
             knownScene);
    if (!vm_net_mock_task_scene_battle_targets_are_well_formed(&task) ||
        !expect_match(&task, 1, 1001, knownScene, true,
                      "mapped-exact-scene-and-monster") ||
        !expect_match(&task, 1, 1001, NULL, false,
                      "mapped-wrong-scene") ||
        !expect_match(&task, 1, 1002, knownScene, false,
                      "mapped-wrong-monster"))
    {
        return 1;
    }

    memset(g_vm_net_mock_task_catalog, 0, sizeof(g_vm_net_mock_task_catalog));
    g_vm_net_mock_task_catalog_count = 1;
    g_vm_net_mock_task_catalog[0] = task;
    memset(g_vm_net_mock_task_catalog[0].requirementScene1, 0,
           sizeof(g_vm_net_mock_task_catalog[0].requirementScene1));
    memset(&targetContext, 0, sizeof(targetContext));
    if (!vm_net_mock_task_scene_battle_target_db_row(
            &targetContext, 3, targetValues, targetLengths) ||
        targetContext.loaded != 1 || targetContext.skipped != 0 ||
        strcmp(g_vm_net_mock_task_catalog[0].requirementScene1, knownScene) != 0)
    {
        fputs("valid scene-battle target mapping row was not applied\n", stderr);
        return 1;
    }
    g_vm_net_mock_task_catalog[0].requirementType1 = 1;
    memset(g_vm_net_mock_task_catalog[0].requirementScene1, 0,
           sizeof(g_vm_net_mock_task_catalog[0].requirementScene1));
    memset(&targetContext, 0, sizeof(targetContext));
    if (!vm_net_mock_task_scene_battle_target_db_row(
            &targetContext, 3, targetValues, targetLengths) ||
        targetContext.loaded != 0 || targetContext.skipped != 1 ||
        g_vm_net_mock_task_catalog[0].requirementScene1[0] != 0)
    {
        fputs("non-kill scene-battle target mapping row was accepted\n", stderr);
        return 1;
    }

    task.requirementType2 = 2;
    task.requirementId2 = 1002;
    task.requirementCount2 = 1;
    snprintf(task.requirementScene2, sizeof(task.requirementScene2), "%s",
             knownScene);
    if (!vm_net_mock_task_scene_battle_targets_are_well_formed(&task) ||
        !expect_match(&task, 2, 1002, knownScene, true,
                      "second-mapped-objective") ||
        !expect_match(&task, 2, 1002, NULL, false,
                      "second-mapped-wrong-scene"))
    {
        return 1;
    }

    task.requirementType1 = 1;
    if (vm_net_mock_task_scene_battle_targets_are_well_formed(&task))
    {
        fputs("mapped collection objective was accepted\n", stderr);
        return 1;
    }
    task.requirementType1 = 2;
    task.requirementId1 = 0x10000u;
    if (vm_net_mock_task_scene_battle_targets_are_well_formed(&task))
    {
        fputs("mapped out-of-range scene monster id was accepted\n", stderr);
        return 1;
    }

    puts("scene battle task objective regression passed");
    return 0;
}
