/*
 * Regression for one-response/one-platform-event delivery.
 *
 * A normal service response consumes exactly one scheduler entry.  Queue
 * capacity must not cause the host to invent a companion data event or retain
 * a parser-derived bootstrap fragment for a later callback.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_client_program_main
#include "../src/main.c"
#undef main

int main(void)
{
    const u32 slot = VM_SCHED_MAX_NET_TASKS - 1u;

    memset(g_netTasks, 0, sizeof(g_netTasks));
    for (u32 index = 0; index < slot; ++index)
    {
        g_netTasks[index].active = 1;
        g_netTasks[index].eventType = 7;
        g_netTasks[index].r0 = 0x40000000u + index;
    }
    g_schedulerTick = 31;
    if (!scheduler_queue_net_event(7, 0x11110000u, 202u, 202u,
                                   0x0103489bu, 0x02000001u) ||
        !g_netTasks[slot].active || g_netTasks[slot].eventType != 7 ||
        g_netTasks[slot].r0 != 0x11110000u ||
        g_netTasks[slot].r1 != 202u || g_netTasks[slot].r2 != 202u ||
        g_netTasks[slot].callback != 0x0103489bu ||
        g_netTasks[slot].context != 0x02000001u ||
        scheduler_count_active_net_tasks() != VM_SCHED_MAX_NET_TASKS)
    {
        fputs("one response did not occupy exactly one normal event slot\n",
              stderr);
        return 1;
    }

    puts("login equipment one-event delivery regression passed");
    return 0;
}
