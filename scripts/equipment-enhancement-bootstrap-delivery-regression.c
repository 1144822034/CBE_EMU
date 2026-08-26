/*
 * Regression for the equipment-stage bootstrap transport boundary.
 *
 * The full 30/21 backpack seed and its 7/7 equipment completion are split
 * into two normal data events.  A single vacant scheduler slot must not admit
 * the primary event by itself; the retained completion is retried before a
 * later response can overtake it.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_client_program_main
#include "../src/main.c"
#undef main

static void fill_net_task_slots(u32 activeCount)
{
    memset(g_netTasks, 0, sizeof(g_netTasks));
    if (activeCount > VM_SCHED_MAX_NET_TASKS)
        activeCount = VM_SCHED_MAX_NET_TASKS;
    for (u32 index = 0; index < activeCount; ++index)
    {
        g_netTasks[index].active = 1;
        g_netTasks[index].eventType = 7;
        g_netTasks[index].r0 = 0x40000000u + index;
    }
}

static int expect_pair_requires_two_slots(void)
{
    vm_net_task before[VM_SCHED_MAX_NET_TASKS];

    fill_net_task_slots(VM_SCHED_MAX_NET_TASKS - 1u);
    memcpy(before, g_netTasks, sizeof(before));
    if (scheduler_queue_net_event_pair_atomic(
            7, 0x11110000u, 101u, 101u, 7, 0x22220000u, 202u, 202u,
            0x0103489bu, 0x02000001u) ||
        memcmp(before, g_netTasks, sizeof(before)) != 0)
    {
        fputs("one free scheduler slot admitted a partial bootstrap pair\n",
              stderr);
        return 1;
    }
    return 0;
}

static int expect_pair_preserves_order(void)
{
    fill_net_task_slots(VM_SCHED_MAX_NET_TASKS - 2u);
    if (!scheduler_queue_net_event_pair_atomic(
            7, 0x11110000u, 101u, 101u, 7, 0x22220000u, 202u, 202u,
            0x0103489bu, 0x02000001u) ||
        g_netTasks[VM_SCHED_MAX_NET_TASKS - 2u].r0 != 0x11110000u ||
        g_netTasks[VM_SCHED_MAX_NET_TASKS - 2u].r1 != 101u ||
        g_netTasks[VM_SCHED_MAX_NET_TASKS - 1u].r0 != 0x22220000u ||
        g_netTasks[VM_SCHED_MAX_NET_TASKS - 1u].r1 != 202u)
    {
        fputs("two-slot bootstrap pair was not queued atomically in order\n",
              stderr);
        return 1;
    }
    return 0;
}

static int expect_blocked_bootstrap_is_retained(void)
{
    vm_client_completion completion;

    memset(&completion, 0, sizeof(completion));
    completion.generation = g_vmClientAsync.generation;
    completion.sequence = 77;
    completion.responseLen = 234;
    completion.followupLen = 3683;
    if (!vm_client_retry_login_bootstrap_delivery(&completion, "net-queue") ||
        completion.deliveryRetryCount != 1 ||
        g_vmClientAsync.completionHead != &completion ||
        g_vmClientAsync.completionTail != &completion)
    {
        fputs("blocked bootstrap completion was not retained for retry\n", stderr);
        return 1;
    }
    g_vmClientAsync.completionHead = NULL;
    g_vmClientAsync.completionTail = NULL;
    completion.next = NULL;
    return 0;
}

int main(void)
{
    Global_R9 = 0;
    g_schedulerTick = 0;
    if (expect_pair_requires_two_slots() != 0 ||
        expect_pair_preserves_order() != 0 ||
        expect_blocked_bootstrap_is_retained() != 0)
    {
        return 1;
    }
    puts("equipment enhancement bootstrap atomic delivery regression passed");
    return 0;
}
