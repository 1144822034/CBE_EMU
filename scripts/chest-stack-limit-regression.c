/*
 * Regression for the client-defined ordinary-item stack limit.
 *
 * This test has no listener and no MySQL calls.  It loads the read-only
 * item.dsh catalogue through the normal service helper, migrates the exact
 * historical invalid state that caused the chest wait (gold chest seq 78,
 * count 121), then verifies that consuming the selected stack leaves a legal
 * 98 + 22 representation.
 *
 * Build from the repository root (Windows MinGW example):
 *   gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11
 *       -ffunction-sections -fdata-sections
 *       scripts/chest-stack-limit-regression.c obj/server/gifDecode.o
 *       obj/server/mystd.o obj/server/mysql-client.o obj/server/md5.o
 *       -Wl,--gc-sections -o tmp/chest-stack-limit-regression.exe
 *       -lpthread -liconv -lm -lkernel32 -lws2_32
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

static u32 count_item_rows(const vm_net_mock_role_state *role, u32 itemId,
                           u32 *firstCountOut, u32 *secondCountOut)
{
    u32 matched = 0;
    if (firstCountOut)
        *firstCountOut = 0;
    if (secondCountOut)
        *secondCountOut = 0;
    if (role == NULL)
        return 0;
    for (u32 i = 0; i < role->backpackItemCount; ++i)
    {
        const vm_net_mock_backpack_item_state *item = &role->backpackItems[i];
        if (item->itemId != itemId)
            continue;
        if (matched == 0 && firstCountOut)
            *firstCountOut = item->count;
        if (matched == 1 && secondCountOut)
            *secondCountOut = item->count;
        ++matched;
    }
    return matched;
}

int main(void)
{
    vm_net_mock_role_state role;
    vm_net_mock_backpack_item_state *gold = NULL;
    u16 keySeq = 0;
    u16 rewardSeq = 0;
    u32 first = 0;
    u32 second = 0;
    u32 remaining = 0;

    memset(&role, 0, sizeof(role));
    role.roleId = 90001;
    role.backpackCapacity = 20;
    role.backpackItemCount = 1;
    role.nextBackpackSeq = 79;
    role.backpackItems[0].itemId = 524;
    role.backpackItems[0].seq = 78;
    role.backpackItems[0].count = 121;

    if (vm_net_mock_item_effect_stack_limit(524) != 99 ||
        vm_net_mock_item_effect_stack_limit(815) != 99)
    {
        fputs("item.dsh chest/key stack limits were not loaded as 99\n", stderr);
        return 1;
    }
    if (!vm_net_mock_role_normalize_backpack(&role))
    {
        fputs("legacy 121-count golden chest did not report a durable backpack migration\n", stderr);
        return 1;
    }
    if (count_item_rows(&role, 524, &first, &second) != 2 ||
        first != 99 || second != 22 || role.backpackItems[0].seq != 78 ||
        role.backpackItems[1].seq == 78)
    {
        fputs("legacy 121-count golden chest was not split into 99 + 22 sequences\n", stderr);
        return 1;
    }

    if (!vm_net_mock_role_add_backpack_item_to_role_in_memory(
            &role, 815, 12, &keySeq) || keySeq == 0 ||
        !vm_net_mock_role_consume_backpack_item(&role, 524, 78, 1, &remaining) ||
        remaining != 98 ||
        !vm_net_mock_role_consume_backpack_item(&role, 815, keySeq, 1, &remaining) ||
        remaining != 11 ||
        !vm_net_mock_role_add_backpack_item_to_role_in_memory(
            &role, 814, 1, &rewardSeq) || rewardSeq == 0)
    {
        fputs("stack-limited chest transaction could not consume/add legal rows\n", stderr);
        return 1;
    }

    gold = vm_net_mock_role_find_backpack_item(&role, 524, 78);
    if (gold == NULL || gold->count != 98 ||
        count_item_rows(&role, 524, &first, &second) != 2 ||
        first != 98 || second != 22)
    {
        fputs("selected golden chest sequence did not remain the only consumed row\n", stderr);
        return 1;
    }
    for (u32 i = 0; i < role.backpackItemCount; ++i)
    {
        const vm_net_mock_backpack_item_state *item = &role.backpackItems[i];
        u32 limit = vm_net_mock_item_effect_stack_limit(item->itemId);
        if (limit != 0 && item->itemId != 921 && item->count > limit)
        {
            fprintf(stderr, "item %u sequence %u exceeds stack limit %u\n",
                    item->itemId, item->seq, limit);
            return 1;
        }
    }

    puts("chest stack-limit regression passed: 121 -> 99 + 22; selected row 99 -> 98");
    return 0;
}
