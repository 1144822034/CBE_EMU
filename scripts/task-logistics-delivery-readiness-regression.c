/*
 * Deterministic contract regression for a logistics task.  It checks the
 * server-side predicate that guards the existing NPC-dialog 6/6 state update;
 * no server, client, socket or database is opened.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    vm_net_mock_role_state role;
    vm_net_mock_task_definition task;

    memset(&role, 0, sizeof(role));
    memset(&task, 0, sizeof(task));
    role.backpackCapacity = 20;
    task.taskId = 2001;
    task.givenItemId = 26;
    task.givenItemCount = 2;

    if (vm_net_mock_task_delivery_is_ready(&role, &task, 0, 0))
    {
        fputs("logistics task became submittable without its cargo\n", stderr);
        return 1;
    }

    role.backpackItemCount = 1;
    role.backpackItems[0].itemId = 26;
    role.backpackItems[0].seq = 101;
    role.backpackItems[0].count = 1;
    if (vm_net_mock_task_delivery_is_ready(&role, &task, 0, 0))
    {
        fputs("logistics task accepted an incomplete cargo stack\n", stderr);
        return 1;
    }

    role.backpackItems[0].count = 2;
    if (!vm_net_mock_task_delivery_is_ready(&role, &task, 0, 0))
    {
        fputs("logistics task did not become submittable with its cargo\n",
              stderr);
        return 1;
    }

    task.givenItemId = 0;
    task.givenItemCount = 0;
    task.requirementType1 = 2;
    task.requirementCount1 = 1;
    if (vm_net_mock_task_delivery_is_ready(&role, &task, 0, 0) ||
        !vm_net_mock_task_delivery_is_ready(&role, &task, 1, 0))
    {
        fputs("existing task progress thresholds changed\n", stderr);
        return 1;
    }

    puts("task logistics delivery readiness regression passed");
    return 0;
}
