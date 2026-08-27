/*
 * Contract regression for automatic task-ID assignment.  The helper receives
 * the complete admin catalog, so built-in rows, overrides and custom rows all
 * reserve their IDs before the new-task form is rendered or saved.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

int main(void)
{
    vm_net_mock_task_definition tasks[VM_NET_MOCK_TASK_CATALOG_MAX];
    u32 allocated = 0;

    memset(tasks, 0, sizeof(tasks));
    tasks[0].taskId = 1;
    tasks[1].taskId = 1999;
    tasks[2].taskId = 2000;
    tasks[3].taskId = 2002;
    if (!vm_mock_admin_find_first_unused_task_id(tasks, 4, &allocated) ||
        allocated != 2001)
    {
        fprintf(stderr, "task ID allocator did not select the first gap\n");
        return 1;
    }

    for (u32 i = 0; i < VM_NET_MOCK_TASK_CATALOG_MAX; ++i)
        tasks[i].taskId = VM_MOCK_ADMIN_NEW_TASK_ID_MIN + i;
    if (!vm_mock_admin_find_first_unused_task_id(
            tasks, VM_NET_MOCK_TASK_CATALOG_MAX, &allocated) ||
        allocated != VM_MOCK_ADMIN_NEW_TASK_ID_MIN +
                         VM_NET_MOCK_TASK_CATALOG_MAX)
    {
        fprintf(stderr, "task ID allocator did not advance past used IDs\n");
        return 1;
    }

    puts("admin task ID allocation regression passed");
    return 0;
}
