/*
 * Deterministic regression for task hand-ins that consume task-owned items.
 *
 * No socket, client process or database is opened.  The fixture exercises the
 * same in-memory consumption helper used by the 6/4 commit path, then checks
 * the native seqnum/iteminfo deletion stream consumed by the CBE case-4
 * response parser.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static bool expect_bytes(const u8 *actual, u32 actualLen,
                         const u8 *expected, u32 expectedLen)
{
    return actual != NULL && expected != NULL && actualLen == expectedLen &&
           memcmp(actual, expected, expectedLen) == 0;
}

int main(void)
{
    vm_net_mock_role_state role;
    vm_net_mock_role_state before;
    vm_net_mock_task_definition task;
    u32 itemIds[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u32 itemCounts[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u16 consumedSeqs[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u8 consumedRemainings[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u8 itemCount = 0;
    u8 consumedCount = 0;
    u8 itemInfo[VM_NET_MOCK_TASK_SUBMIT_ITEMINFO_MAX_BYTES];
    u32 itemInfoLen = 0;
    static const u8 expectedItemInfo[] = {
        0x00, 0x02, 0x00, 0x65, 0x00, 0x01, 0x00,
        0x00, 0x02, 0x00, 0x66, 0x00, 0x01, 0x00};

    memset(&role, 0, sizeof(role));
    memset(&task, 0, sizeof(task));
    role.backpackCapacity = 20;
    role.backpackItemCount = 2;
    role.backpackItems[0].itemId = 42;
    role.backpackItems[0].seq = 101;
    role.backpackItems[0].count = 2;
    role.backpackItems[1].itemId = 26;
    role.backpackItems[1].seq = 102;
    role.backpackItems[1].count = 1;
    task.taskId = 2000;
    task.requirementType1 = 1;
    task.requirementId1 = 42;
    task.requirementCount1 = 2;
    task.givenItemId = 26;
    task.givenItemCount = 1;

    if (!vm_net_mock_task_collect_consumed_items(&task, itemIds, itemCounts,
                                                 &itemCount) ||
        itemCount != 2 || itemIds[0] != 42 || itemCounts[0] != 2 ||
        itemIds[1] != 26 || itemCounts[1] != 1)
    {
        fputs("task delivery consumption list omitted a given item\n", stderr);
        return 1;
    }
    if (!vm_net_mock_task_consume_items(&role, &task, consumedSeqs,
                                        consumedRemainings, &consumedCount) ||
        consumedCount != 2 || consumedSeqs[0] != 101 ||
        consumedSeqs[1] != 102 || consumedRemainings[0] != 0 ||
        consumedRemainings[1] != 0 || role.backpackItemCount != 0)
    {
        fputs("task delivery items were not consumed from the role snapshot\n",
              stderr);
        return 1;
    }
    if (!vm_net_mock_build_task_submit_iteminfo(
            itemInfo, sizeof(itemInfo), &itemInfoLen, consumedSeqs,
            consumedRemainings, consumedCount) ||
        !expect_bytes(itemInfo, itemInfoLen, expectedItemInfo,
                      sizeof(expectedItemInfo)))
    {
        fputs("task submit iteminfo did not encode tagged sequence/count rows\n",
              stderr);
        return 1;
    }

    /* A task iteminfo count is one byte. Reject an unrepresentable remaining
     * stack before committing state rather than silently truncating it. */
    memset(&role, 0, sizeof(role));
    role.backpackCapacity = 20;
    role.backpackItemCount = 1;
    role.backpackItems[0].itemId = 42;
    role.backpackItems[0].seq = 103;
    role.backpackItems[0].count = 257;
    memset(&task, 0, sizeof(task));
    task.requirementType1 = 1;
    task.requirementId1 = 42;
    task.requirementCount1 = 1;
    before = role;
    if (vm_net_mock_task_consume_items(&role, &task, consumedSeqs,
                                       consumedRemainings, &consumedCount) ||
        memcmp(&role, &before, sizeof(role)) != 0)
    {
        fputs("unrepresentable task item remainder mutated the role\n", stderr);
        return 1;
    }

    puts("task delivery item consumption regression passed");
    return 0;
}
