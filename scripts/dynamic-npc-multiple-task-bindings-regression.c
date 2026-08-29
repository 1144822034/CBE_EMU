/*
 * Pure in-process coverage for the bounded dynamic-NPC task-binding model.
 * It does not open a database or start a service.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    vm_net_mock_dynamic_npc_task_binding parsed[VM_NET_MOCK_DYNAMIC_NPC_TASK_MAX];
    vm_net_mock_dynamic_npc_task_binding resolved[VM_NET_MOCK_DYNAMIC_NPC_TASK_MAX];
    vm_net_mock_scene_npcinfo_seed seed;
    u32 parsedCount = 0;
    u32 resolvedCount = 0;
    char tooMany[160];
    size_t offset = 0;

    memset(parsed, 0, sizeof(parsed));
    memset(resolved, 0, sizeof(resolved));
    memset(&seed, 0, sizeof(seed));
    memset(g_vm_net_mock_dynamic_npc_overrides, 0,
           sizeof(g_vm_net_mock_dynamic_npc_overrides));

    if (!vm_net_mock_dynamic_npc_task_bindings_parse(
            "2000:1,2001:4", strlen("2000:1,2001:4"), parsed,
            VM_NET_MOCK_DYNAMIC_NPC_TASK_MAX, &parsedCount) ||
        parsedCount != 2 || parsed[0].taskId != 2000 ||
        parsed[0].repeatPolicy != VM_NET_MOCK_TASK_REPEAT_UNLIMITED ||
        parsed[1].taskId != 2001 ||
        parsed[1].repeatPolicy != VM_NET_MOCK_TASK_REPEAT_MONTHLY ||
        vm_net_mock_dynamic_npc_task_bindings_parse(
            "2000:0,2000:1", strlen("2000:0,2000:1"), parsed,
            VM_NET_MOCK_DYNAMIC_NPC_TASK_MAX, &parsedCount))
    {
        fputs("dynamic NPC task binding parser regressed\n", stderr);
        return 1;
    }
    for (u32 index = 0; index <= VM_NET_MOCK_DYNAMIC_NPC_TASK_MAX; ++index)
    {
        int written = snprintf(tooMany + offset, sizeof(tooMany) - offset,
                               "%s%u:0", index == 0 ? "" : ",",
                               3000u + index);
        if (written < 0 || (size_t)written >= sizeof(tooMany) - offset)
        {
            fputs("multiple-task fixture overflow\n", stderr);
            return 1;
        }
        offset += (size_t)written;
    }
    if (vm_net_mock_dynamic_npc_task_bindings_parse(
            tooMany, offset, parsed, VM_NET_MOCK_DYNAMIC_NPC_TASK_MAX,
            &parsedCount))
    {
        fputs("dynamic NPC task binding limit regressed\n", stderr);
        return 1;
    }

    g_vm_net_mock_dynamic_npc_override_count = 1;
    snprintf(g_vm_net_mock_dynamic_npc_overrides[0].scene,
             sizeof(g_vm_net_mock_dynamic_npc_overrides[0].scene), "%s",
             "multiple-task.sce");
    g_vm_net_mock_dynamic_npc_overrides[0].seed.actorId = 61001;
    g_vm_net_mock_dynamic_npc_overrides[0].taskBindingCount = 2;
    g_vm_net_mock_dynamic_npc_overrides[0].taskBindings[0].taskId = 2000;
    g_vm_net_mock_dynamic_npc_overrides[0].taskBindings[0].repeatPolicy =
        VM_NET_MOCK_TASK_REPEAT_UNLIMITED;
    g_vm_net_mock_dynamic_npc_overrides[0].taskBindings[1].taskId = 2001;
    g_vm_net_mock_dynamic_npc_overrides[0].taskBindings[1].repeatPolicy =
        VM_NET_MOCK_TASK_REPEAT_MONTHLY;
    seed.actorId = 61001;
    seed.taskId = 9999; /* An override must win over this legacy projection. */
    resolvedCount = vm_net_mock_dynamic_npc_task_bindings_resolve(
        "multiple-task.sce", &seed, resolved,
        VM_NET_MOCK_DYNAMIC_NPC_TASK_MAX);
    if (resolvedCount != 2 || resolved[0].taskId != 2000 ||
        resolved[1].taskId != 2001 ||
        resolved[1].repeatPolicy != VM_NET_MOCK_TASK_REPEAT_MONTHLY)
    {
        fputs("dynamic NPC task binding runtime resolution regressed\n", stderr);
        return 1;
    }

    puts("dynamic NPC multiple task bindings regression passed");
    return 0;
}
