/*
 * Regression for the client-side platform network boundary.
 *
 * The packets model the WT30/1, WT30/2 and WT18/7 frames involved in a
 * remote scene update.  The host must treat every frame as opaque data and
 * queue event-7 with the callback/context that the CBE registered.  It must
 * not extract scene lifecycle state or select a different callback.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_client_program_main
#include "../src/main.c"
#undef main

static bool assert_single_event(const char *name, u32 responsePtr,
                                u32 responseLen, u32 callback, u32 context)
{
    vm_net_task *task;

    memset(g_netTasks, 0, sizeof(g_netTasks));
    if (!scheduler_queue_net_event(7, responsePtr, responseLen, responseLen,
                                   callback, context))
    {
        fprintf(stderr, "%s was not queued\n", name);
        return false;
    }
    task = &g_netTasks[0];
    if (!task->active || task->eventType != 7 || task->r0 != responsePtr ||
        task->r1 != responseLen || task->r2 != responseLen ||
        task->callback != callback || task->context != context)
    {
        fprintf(stderr, "%s changed the CBE event contract\n", name);
        return false;
    }
    return true;
}

int main(void)
{
    static const u8 wt30_1[] = {
        'W', 'T', 0, 11, 1, 1, 30, 1, 0, 0, 6};
    static const u8 wt30_2[] = {
        'W', 'T', 0, 11, 1, 1, 30, 2, 0, 0, 6};
    static const u8 wt18_7[] = {
        'W', 'T', 0, 11, 1, 1, 18, 7, 0, 0, 6};
    const u32 callback = 0x0103489bu;
    const u32 context = 0x010560f4u;

    g_schedulerTick = 100;
    if (!assert_single_event("WT30/1", 0x0500f000u, sizeof(wt30_1),
                             callback, context) ||
        !assert_single_event("WT30/2", 0x0500f100u, sizeof(wt30_2),
                             callback, context) ||
        !assert_single_event("WT18/7", 0x0500f200u, sizeof(wt18_7),
                             callback, context))
    {
        return 1;
    }

    memset(g_netTasks, 0, sizeof(g_netTasks));
    if (!scheduler_queue_net_event_pair_atomic(
            7, 0x0500f300u, sizeof(wt30_1), sizeof(wt30_1),
            7, 0x0500f400u, sizeof(wt18_7), sizeof(wt18_7), callback,
            context) ||
        !g_netTasks[0].active || !g_netTasks[1].active ||
        g_netTasks[0].callback != callback ||
        g_netTasks[0].context != context ||
        g_netTasks[1].callback != callback ||
        g_netTasks[1].context != context)
    {
        fputs("atomic event-7 pair changed the CBE callback contract\n",
              stderr);
        return 1;
    }

    puts("remote scene platform event regression passed");
    return 0;
}
