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
    vm_net_mock_task_definition task;
    u32 itemIds[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u32 itemCounts[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u16 consumedSeqs[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u8 consumedRemainings[VM_NET_MOCK_TASK_CONSUMED_ITEM_MAX];
    u8 itemCount = 0;
    u8 consumedCount = 0;
    u8 itemInfo[VM_NET_MOCK_TASK_SUBMIT_ITEMINFO_MAX_BYTES];
    u32 itemInfoLen = 0;
    u32 stackLimit = 0;
    vm_net_mock_role_state beforeAccept;
    vm_net_mock_role_state afterAccept;
    vm_net_mock_task_definition deliveryTask;
    u8 acceptResponse[2048];
    u32 acceptResponseLen = 5;
    u8 acceptObjectCount = 0;
    u32 acceptFirstObjectLen = 0;
    u32 acceptSecondObjectOffset = 0;
    u32 acceptSecondObjectLen = 0;
    u8 submitRefreshResponse[256];
    u32 submitRefreshResponseLen = 5;
    u8 submitRefreshObjectCount = 0;
    u32 submitRefreshObjectLen = 0;
    u8 submitCountInfo[32];
    u32 submitCountInfoLen = 0;
    static const u8 expectedItemInfo[] = {
        0x00, 0x02, 0x00, 0x65, 0x00, 0x01, 0x00,
        0x00, 0x02, 0x00, 0x66, 0x00, 0x01, 0x00};
    static const u8 expectedAcceptedItemSubmitInfo[] = {
        0x00, 0x02, 0x00, 0x68, 0x00, 0x01, 0x00};
    static const u8 expectedAcceptedItemCountInfo[] = {
        0x00, 0x01, 0x01, 0x00, 0x02, 0x00, 0x68,
        0x00, 0x04, 0x00, 0x00, 0x00, 0x00};

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

    /* `item.dsh` owns the actual client-visible stack maximum.  Set the row to
     * that exact limit so normalization does not split it before the case-4
     * iteminfo contract is checked. */
    stackLimit = vm_net_mock_item_effect_stack_limit(42);
    if (stackLimit == 0 || stackLimit > 0xffu)
    {
        fputs("task material stack limit is outside case-4 iteminfo range\n",
              stderr);
        return 1;
    }
    memset(&role, 0, sizeof(role));
    role.backpackCapacity = 20;
    role.backpackItemCount = 1;
    role.backpackItems[0].itemId = 42;
    role.backpackItems[0].seq = 103;
    role.backpackItems[0].count = stackLimit;
    memset(&task, 0, sizeof(task));
    task.requirementType1 = 1;
    task.requirementId1 = 42;
    task.requirementCount1 = 1;
    const bool maximumConsumed = vm_net_mock_task_consume_items(
        &role, &task, consumedSeqs, consumedRemainings, &consumedCount);
    if (!maximumConsumed ||
        consumedCount != 1 || consumedSeqs[0] != 103 ||
        consumedRemainings[0] != stackLimit - 1 || role.backpackItemCount != 1 ||
        role.backpackItems[0].count != stackLimit - 1)
    {
        fprintf(stderr,
                "maximum task item remainder mismatch consumed=%u rows=%u seq=%u remaining=%u stored=%u limit=%u\n",
                maximumConsumed ? 1u : 0u, role.backpackItemCount,
                consumedSeqs[0], consumedRemainings[0],
                role.backpackItems[0].count, stackLimit);
        return 1;
    }

    /* A logistics accept runs before the backpack page can issue another
     * query.  Its 6/11 response must therefore create the given row through
     * 7/15 and immediately establish the same sequence's absolute count via
     * 7/11.  The later 6/4 iteminfo in this test consumes that exact native
     * sequence, so a stale client row cannot be hidden by a re-login list. */
    memset(&beforeAccept, 0, sizeof(beforeAccept));
    memset(&afterAccept, 0, sizeof(afterAccept));
    memset(&deliveryTask, 0, sizeof(deliveryTask));
    memset(acceptResponse, 0, sizeof(acceptResponse));
    beforeAccept.backpackCapacity = 20;
    afterAccept.backpackCapacity = 20;
    afterAccept.backpackItemCount = 1;
    afterAccept.backpackItems[0].itemId = 26;
    afterAccept.backpackItems[0].seq = 104;
    afterAccept.backpackItems[0].count = 1;
    deliveryTask.taskId = 2000;
    deliveryTask.givenItemId = 26;
    deliveryTask.givenItemCount = 1;
    if (!vm_net_mock_append_task_accept_backpack_refresh(
            acceptResponse, sizeof(acceptResponse), &acceptResponseLen,
            &acceptObjectCount, &beforeAccept, &afterAccept, &deliveryTask) ||
        acceptObjectCount != 2)
    {
        fputs("task accept did not build the backpack delta objects\n", stderr);
        return 1;
    }
    vm_net_mock_finish_wt_packet(acceptResponse, acceptResponseLen,
                                 acceptObjectCount);
    /* Responses use a six-byte object header beginning after the packet's
     * object-count byte, unlike the five-byte request-object helper. */
    acceptFirstObjectLen = ((u32)acceptResponse[9] << 8) | acceptResponse[10];
    acceptSecondObjectOffset = 5 + acceptFirstObjectLen;
    if (acceptResponse[0] != 'W' || acceptResponse[1] != 'T' ||
        acceptResponse[4] != 2 || acceptResponse[5] != 1 ||
        acceptResponse[6] != 7 || acceptResponse[7] != 15 ||
        acceptFirstObjectLen <= 6 ||
        acceptSecondObjectOffset + 6 > acceptResponseLen ||
        acceptResponse[acceptSecondObjectOffset] != 1 ||
        acceptResponse[acceptSecondObjectOffset + 1] != 7 ||
        acceptResponse[acceptSecondObjectOffset + 2] != 11)
    {
        fputs("task accept backpack delta wire contract is incomplete\n", stderr);
        return 1;
    }
    acceptSecondObjectLen =
        ((u32)acceptResponse[acceptSecondObjectOffset + 4] << 8) |
        acceptResponse[acceptSecondObjectOffset + 5];
    if (acceptSecondObjectLen <= 6 ||
        acceptSecondObjectOffset + acceptSecondObjectLen != acceptResponseLen)
    {
        fputs("task accept backpack delta response is truncated\n", stderr);
        return 1;
    }
    if (!vm_net_mock_task_consume_items(&afterAccept, &deliveryTask,
                                        consumedSeqs, consumedRemainings,
                                        &consumedCount) ||
        consumedCount != 1 || consumedSeqs[0] != 104 ||
        consumedRemainings[0] != 0 || afterAccept.backpackItemCount != 0 ||
        !vm_net_mock_build_task_submit_iteminfo(
            itemInfo, sizeof(itemInfo), &itemInfoLen, consumedSeqs,
            consumedRemainings, consumedCount) ||
        !expect_bytes(itemInfo, itemInfoLen, expectedAcceptedItemSubmitInfo,
                      sizeof(expectedAcceptedItemSubmitInfo)))
    {
        fputs("task submit did not remove the sequence granted at accept\n",
              stderr);
        return 1;
    }
    memset(submitRefreshResponse, 0, sizeof(submitRefreshResponse));
    if (!vm_net_mock_build_item_use_count_info_blob(
            submitCountInfo, sizeof(submitCountInfo), consumedSeqs[0],
            consumedRemainings[0], &submitCountInfoLen) ||
        !expect_bytes(submitCountInfo, submitCountInfoLen,
                      expectedAcceptedItemCountInfo,
                      sizeof(expectedAcceptedItemCountInfo)) ||
        !vm_net_mock_append_task_submit_backpack_refresh(
            submitRefreshResponse, sizeof(submitRefreshResponse),
            &submitRefreshResponseLen, &submitRefreshObjectCount,
            &deliveryTask, consumedSeqs, consumedRemainings, consumedCount) ||
        submitRefreshObjectCount != 1)
    {
        fputs("task submit did not build the zero-count backpack refresh\n",
              stderr);
        return 1;
    }
    vm_net_mock_finish_wt_packet(submitRefreshResponse,
                                 submitRefreshResponseLen,
                                 submitRefreshObjectCount);
    submitRefreshObjectLen =
        ((u32)submitRefreshResponse[9] << 8) | submitRefreshResponse[10];
    if (submitRefreshResponse[0] != 'W' || submitRefreshResponse[1] != 'T' ||
        submitRefreshResponse[4] != 1 || submitRefreshResponse[5] != 1 ||
        submitRefreshResponse[6] != 7 || submitRefreshResponse[7] != 11 ||
        submitRefreshObjectLen <= 6 ||
        5 + submitRefreshObjectLen != submitRefreshResponseLen)
    {
        fputs("task submit backpack refresh wire contract is incomplete\n",
              stderr);
        return 1;
    }

    puts("task delivery item consumption regression passed");
    return 0;
}
