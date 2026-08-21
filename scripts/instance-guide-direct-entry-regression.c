/*
 * Regression for dynamic NPC instance-guide first-click routing.
 *
 * A guide with an authoritative target scene must expose the existing
 * ENTER_INSTANCE action directly, so the first task-hall action=1 request is
 * resolved by the normal 30/1 scene-enter builder.  A separately configured
 * guard challenge must use the first-dialog action-13 path, avoiding both an
 * instance-operation dialog and the native 30/9 confirmation window.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

int main(void)
{
    vm_net_mock_scene_npcinfo_seed seed;
    const char *name = NULL;
    const char *description = NULL;
    u8 response[256];
    u32 responseLen = 0;
    u32 value = 0;
    const u32 actorId = 39991u;

    memset(&seed, 0, sizeof(seed));
    seed.actorId = actorId;
    snprintf(seed.instanceScene, sizeof(seed.instanceScene), "%s",
             "b_29\xC3\xCE\xBE\xB3\xBF\xD5\xBC\xE4.sce"); /* 梦境空间 */
    seed.instanceX = 50;
    seed.instanceY = 50;
    seed.challengeEnemyId = 105;
    if (!vm_net_mock_npc_service_option_default(
            &seed, VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE, &name, &description,
            &value) ||
        value != (VM_NET_MOCK_NPC_SERVICE_ENTER_INSTANCE_BASE | actorId) ||
        name == NULL || description == NULL)
    {
        fputs("configured instance guide did not route first selection to direct entry\n",
              stderr);
        return 1;
    }
    memset(response, 0, sizeof(response));
    responseLen = vm_net_mock_build_instance_enter_response(
        &seed, response, sizeof(response));
    /* Response objects use the client event-packet layout: the count is at
     * byte 4 and the first object's major/kind/subtype are 5/6/7. */
    if (responseLen < 11 || response[0] != 'W' || response[1] != 'T' ||
        response[4] != 1 || response[5] != 1 || response[6] != 30 ||
        response[7] != 1)
    {
        fputs("direct instance entry did not produce one 30/1 scene response\n",
              stderr);
        return 1;
    }

    value = 0;
    if (!vm_net_mock_npc_service_option_default(
            &seed, VM_NET_MOCK_NPC_KIND_INSTANCE_CHALLENGE, &name, &description,
            &value) ||
        value != (VM_NET_MOCK_NPC_SERVICE_CHALLENGE_INSTANCE_BASE | actorId) ||
        !vm_net_mock_npc_service_is_direct_instance_challenge(
            &seed, VM_NET_MOCK_NPC_KIND_INSTANCE_CHALLENGE) ||
        vm_net_mock_npc_service_is_direct_instance_challenge(
            &seed, VM_NET_MOCK_NPC_KIND_INSTANCE_GUIDE))
    {
        fputs("dedicated guard challenge did not use direct action-13 routing\n",
              stderr);
        return 1;
    }

    puts("instance guide direct-entry regression passed: direct 26/1 entry + action-13 guard challenge");
    return 0;
}
