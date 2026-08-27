/*
 * Client-side regression for the retired login-equipment splitter.
 *
 * The transport must not recognise WT 30/21 or 7/7 payload semantics in
 * order to turn one service response into multiple guest callbacks.  This
 * focused fixture checks the remaining platform operation: queue the exact
 * event type, response range, callback and context selected by the CBE.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_client_program_main
#include "../src/main.c"
#undef main

int main(void)
{
    const u32 responsePtr = 0x01012000u;
    const u32 responseLen = 0x7d3u;
    const u32 callback = 0x051a6289u;
    const u32 context = 0x01056c10u;

    memset(g_netTasks, 0, sizeof(g_netTasks));
    g_schedulerTick = 77;
    if (!scheduler_queue_net_event(7, responsePtr, responseLen, responseLen,
                                   callback, context) ||
        !g_netTasks[0].active || g_netTasks[0].eventType != 7 ||
        g_netTasks[0].r0 != responsePtr ||
        g_netTasks[0].r1 != responseLen ||
        g_netTasks[0].r2 != responseLen ||
        g_netTasks[0].callback != callback ||
        g_netTasks[0].context != context ||
        g_netTasks[0].delayTicks != 0 || g_netTasks[0].notBeforeTick != 77 ||
        scheduler_count_active_net_tasks() != 1)
    {
        fputs("opaque normal data event was not queued verbatim\n", stderr);
        return 1;
    }

    puts("login equipment opaque platform-event regression passed");
    return 0;
}
