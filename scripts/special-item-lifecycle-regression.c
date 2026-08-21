/*
 * Pure resource/rule regression for the specialised-item lifecycle.
 *
 * It intentionally starts neither server nor client and never opens MySQL.
 * Database transactions are covered by the handler's SQL contract; this test
 * locks down the resource IDs and deterministic rules that decide which
 * lifecycle owns each item:
 *
 * - 战斗心得 (828): 60 minutes, +20% effect is the only source-proven
 *   continuous-hangup ceiling, exactly 200 battles.
 * - 聚元丹 (833): special 7/33 flow, not ordinary HP/MP consumption.
 * - 修炼天书 instance (921): transfer only when its level is higher than the
 *   recipient; the default generated record is level 10 / 43612 EXP.
 * - 行酒令、月饼、爱国之心 (804/812/938): stackable offline tokens, each
 *   funding one hour at the normal-practise level-interval rate.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

static int expect_item(u32 itemId, u8 category, u8 stack, u8 consumeMode,
                       u8 durationMinutes)
{
    const vm_net_mock_item_effect_catalog_item *item =
        vm_net_mock_find_item_effect_catalog_item(itemId);

    if (item == NULL || item->category != category || item->stack != stack ||
        item->consumeMode != consumeMode ||
        item->durationMinutes != durationMinutes)
    {
        fprintf(stderr,
                "item %u contract mismatch: category=%u stack=%u consume=%u duration=%u\n",
                itemId, item ? item->category : 0, item ? item->stack : 0,
                item ? item->consumeMode : 0,
                item ? item->durationMinutes : 0);
        return 1;
    }
    return 0;
}

int main(void)
{
    vm_net_mock_role_state role;
    u32 remaining = 0;
    u32 expectedOfflineExp = 0;

    if (!vm_net_mock_set_resource_dir("web/fs/JHOnlineData") ||
        vm_net_mock_load_item_effect_catalog() == 0)
    {
        fputs("unable to load isolated item.dsh resource\n", stderr);
        return 1;
    }

    if (expect_item(828, 10, 99, 1, 60) != 0 ||
        expect_item(833, 10, 99, 1, 0) != 0 ||
        expect_item(804, 14, 99, 1, 0) != 0 ||
        expect_item(812, 14, 99, 1, 0) != 0 ||
        expect_item(938, 14, 99, 1, 0) != 0)
    {
        return 1;
    }
    if (!vm_net_mock_item_requires_special_use_protocol(828) ||
        !vm_net_mock_item_requires_special_use_protocol(833) ||
        !vm_net_mock_item_requires_special_use_protocol(921) ||
        VM_NET_MOCK_SCENE_HANGUP_INSIGHT_MAX_BATTLES != 200 ||
        VM_NET_MOCK_OFFLINE_EXP_TOKEN_MINUTES != 60 ||
        VM_NET_MOCK_VITALITY_MAX != 100 ||
        VM_NET_MOCK_VITALITY_PILL_RESTORE != 100)
    {
        fputs("special-item ownership or fixed rule mismatch\n", stderr);
        return 1;
    }

    if (vm_net_mock_role_level_from_exp(
            VM_NET_MOCK_TRAINING_BOOK_DEFAULT_EXPERIENCE) !=
            VM_NET_MOCK_TRAINING_BOOK_DEFAULT_LEVEL ||
        VM_NET_MOCK_TRAINING_BOOK_DEFAULT_EXPERIENCE <
            vm_net_mock_role_level_start_exp(
                VM_NET_MOCK_TRAINING_BOOK_DEFAULT_LEVEL) ||
        vm_net_mock_role_level_from_exp(
            VM_NET_MOCK_TRAINING_BOOK_DEFAULT_EXPERIENCE - 1u) >=
            VM_NET_MOCK_TRAINING_BOOK_DEFAULT_LEVEL)
    {
        fputs("training-book default is not above its recipient threshold\n",
              stderr);
        return 1;
    }

    memset(&role, 0, sizeof(role));
    role.roleId = 601002;
    role.level = 1;
    role.backpackCapacity = 20;
    role.backpackItemCount = 1;
    role.backpackItems[0].itemId = 804;
    role.backpackItems[0].seq = 1;
    role.backpackItems[0].count = 2;
    if (!vm_net_mock_role_consume_backpack_item(&role, 804, 1, 1, &remaining) ||
        remaining != 1 || role.backpackItemCount != 1 ||
        role.backpackItems[0].count != 1)
    {
        fputs("offline-token exact-stack consumption mismatch\n", stderr);
        return 1;
    }

    expectedOfflineExp = 120u * vm_net_mock_practise_exp_per_minute(
                                   vm_net_mock_role_level_start_exp(60), false);
    if (expectedOfflineExp != 19200u)
    {
        fprintf(stderr, "offline EXP policy mismatch: %u\n", expectedOfflineExp);
        return 1;
    }

    printf("special-item-lifecycle-v1 passed: insight=60m/200 battles, "
           "vitality=100/100, book=level%u/exp%u, offline=120m/%u EXP\n",
           VM_NET_MOCK_TRAINING_BOOK_DEFAULT_LEVEL,
           VM_NET_MOCK_TRAINING_BOOK_DEFAULT_EXPERIENCE, expectedOfflineExp);
    return 0;
}
