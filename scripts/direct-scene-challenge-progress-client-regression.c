/* Regression for action13's direct scene-battle transport boundary. The test
 * includes the client aggregation unit so it exercises the same static
 * response splitter as the emulator, without starting a VM, socket, or input
 * automation. */

#include <stdio.h>
#include <string.h>

#define main cbe_client_program_main
#include "../src/main.c"
#undef main

int main(void)
{
    static const u8 sceneBattleStart[] = {
        'W', 'T', 0, 17, 2,
        1, 2, 2, 0, 0, 6,
        1, 4, 5, 0, 0, 6
    };
    u8 primary[sizeof(sceneBattleStart)];
    u8 followup[sizeof(sceneBattleStart)];
    u32 primaryLen = sizeof(primary);
    u32 followupLen = 0;
    vm_client_completion completion;
    vm_net_remote_observation observation;

    memset(followup, 0, sizeof(followup));
    memcpy(primary, sceneBattleStart, sizeof(primary));
    if (vm_client_extract_action13_battle_followup(
            primary, &primaryLen, followup, sizeof(followup), &followupLen) ||
        primaryLen != sizeof(sceneBattleStart) || followupLen != 0 ||
        memcmp(primary, sceneBattleStart, sizeof(primary)) != 0)
    {
        fputs("direct action13 battle was incorrectly split from its owner event\n",
              stderr);
        return 1;
    }

    /* The two battle objects remain one event, preserving the mmBattle owner
     * selected by action13's request path. */
    memset(g_netTasks, 0, sizeof(g_netTasks));
    g_schedulerTick = 701;
    scheduler_queue_net_event(7, 0x00123456u, primaryLen, primaryLen,
                              0, 0x00789abcu);
    if (!g_netTasks[0].active || g_netTasks[0].deferredToNextTick)
    {
        fputs("direct action13 battle was not queued as one normal event\n", stderr);
        return 1;
    }

    memset(&completion, 0, sizeof(completion));
    memset(&observation, 0, sizeof(observation));
    completion.eventType = 7;
    completion.response = (u8 *)sceneBattleStart;
    completion.responseLen = sizeof(sceneBattleStart);
    completion.sequence = 702;
    vm_client_capture_hangup_battle_start_response(&completion, &observation);
    if (!observation.hasHangupBattleStart ||
        observation.hangupBattleStartDirect ||
        observation.hangupResponseObjectCount != 2 ||
        observation.hangupResponseParsedCount != 2 ||
        observation.hangupResponseSequence != completion.sequence ||
        observation.hangupResponseLength != sizeof(sceneBattleStart))
    {
        fputs("two-object scene battle start was not observed\n", stderr);
        return 1;
    }

    puts("direct scene challenge client regression passed: "
         "event7(2/2+4/5) with read-only parser/render observation");
    return 0;
}
