/*
 * Contract regression for immediate recovery items in the normal WT 7/1
 * builder.  This test starts neither client nor service and does not open
 * MySQL; it protects the resource-derived branch which decides whether the
 * response uses the proven silent backpack completion route rather than the
 * persistent 7/1 success modal or the state-producing 7/7 type=2 row.
 */

#include <stdio.h>

#define main cbe_server_program_main
#include "../src/main.c"
#undef main

static int expect_true(bool value, const char *label)
{
    if (!value)
    {
        fprintf(stderr, "expected true: %s\n", label);
        return 1;
    }
    return 0;
}

static int expect_false(bool value, const char *label)
{
    if (value)
    {
        fprintf(stderr, "expected false: %s\n", label);
        return 1;
    }
    return 0;
}

static u32 append_test_wt_object(u8 *packet, u32 pos, u8 kind, u8 subtype)
{
    /* The extractor only needs a valid six-byte object header; keep one
     * payload byte so the length cannot be mistaken for an empty object. */
    packet[pos + 0] = 1;
    packet[pos + 1] = kind;
    packet[pos + 2] = subtype;
    packet[pos + 3] = 0;
    packet[pos + 4] = 0;
    packet[pos + 5] = 7;
    packet[pos + 6] = 0;
    return pos + 7;
}

static int check_remote_followup_order(void)
{
    u8 packet[64] = {'W', 'T', 0, 0, 3};
    u8 follow[64] = {0};
    u32 pos = 5;
    u32 followLen = 0;
    u8 primaryCount;

    pos = append_test_wt_object(packet, pos, 7, 4);
    pos = append_test_wt_object(packet, pos, 7, 11);
    pos = append_test_wt_object(packet, pos, 7, 37);
    packet[2] = (u8)(pos >> 8);
    packet[3] = (u8)pos;

    if (vm_net_mock_extract_item_use_backpack_followup(
            packet, &pos, follow, sizeof(follow), &followLen))
    {
        fputs("unexpected remote split for in-place quantity response\n", stderr);
        return 1;
    }
    primaryCount = packet[4];
    if (primaryCount != 3 || packet[5] != 1 || packet[6] != 7 ||
        packet[7] != 4 || packet[12] != 1 || packet[13] != 7 ||
        packet[14] != 11 || packet[19] != 1 || packet[20] != 7 ||
        packet[21] != 37)
    {
        fputs("in-place quantity response order mismatch\n", stderr);
        return 1;
    }
    return 0;
}

static int check_all_row_quantity_blob(void)
{
    vm_net_mock_role_state role;
    u8 blob[64] = {0};
    u32 blobLen = 0;

    memset(&role, 0, sizeof(role));
    role.backpackCapacity = 20;
    role.backpackItemCount = 3;
    role.backpackItems[0].itemId = 301;
    role.backpackItems[0].seq = 16;
    role.backpackItems[0].count = 9;
    role.backpackItems[1].itemId = 301;
    role.backpackItems[1].seq = 17;
    role.backpackItems[1].count = 10;
    role.backpackItems[2].itemId = 301;
    role.backpackItems[2].seq = 18;
    role.backpackItems[2].count = 19;
    if (!vm_net_mock_build_item_use_count_rows_blob(
            blob, sizeof(blob), &role, 301, 18, 18, &blobLen) ||
        blob[2] != 3 || blobLen != 3 + 10 * 3 ||
        blob[3] != 0 || blob[4] != 2 || blob[5] != 0 || blob[6] != 18 ||
        blob[13] != 0 || blob[14] != 2 || blob[15] != 0 || blob[16] != 16 ||
        blob[23] != 0 || blob[24] != 2 || blob[25] != 0 || blob[26] != 17)
    {
        fputs("all-row quantity blob mismatch\n", stderr);
        return 1;
    }
    return 0;
}

static int check_client_stack_reconciliation(void)
{
    vm_net_mock_role_state role;
    u32 remaining = 0;
    u8 blob[64] = {0};
    u32 blobLen = 0;
    memset(&role, 0, sizeof(role));
    role.backpackCapacity = 20;
    role.backpackItemCount = 4;
    role.backpackItems[0].itemId = 301;
    role.backpackItems[0].seq = 16;
    role.backpackItems[0].count = 9;
    role.backpackItems[1].itemId = 301;
    role.backpackItems[1].seq = 17;
    role.backpackItems[1].count = 10;
    role.backpackItems[2].itemId = 301;
    role.backpackItems[2].seq = 18;
    role.backpackItems[2].count = 13;
    role.backpackItems[3].itemId = 301;
    role.backpackItems[3].seq = 19;
    role.backpackItems[3].count = 4;
    if (!vm_net_mock_role_consume_client_visible_stack(
            &role, 301, 18, 1, &remaining) || remaining != 19 ||
        role.backpackItemCount != 2 ||
        role.backpackItems[0].itemId != 301 ||
        role.backpackItems[0].seq != 18 || role.backpackItems[0].count != 19 ||
        role.backpackItems[1].seq != 16 || role.backpackItems[1].count != 16)
    {
        fputs("client stack reconciliation mismatch\n", stderr);
        return 1;
    }
    if (!vm_net_mock_build_item_use_count_rows_blob(
            blob, sizeof(blob), &role, 301, 18, remaining, &blobLen) ||
        blob[2] != 2 || blobLen != 3 + 2 * 10 ||
        blob[3] != 0 || blob[4] != 2 || blob[5] != 0 || blob[6] != 18 ||
        blob[7] != 0 || blob[8] != 4 || blob[9] != 0 || blob[10] != 0 ||
        blob[11] != 0 || blob[12] != 19 ||
        blob[13] != 0 || blob[14] != 2 || blob[15] != 0 || blob[16] != 16 ||
        blob[17] != 0 || blob[18] != 4 || blob[19] != 0 || blob[20] != 0 ||
        blob[21] != 0 || blob[22] != 16)
    {
        fputs("reconciled quantity blob mismatch\n", stderr);
        return 1;
    }
    memset(&role, 0, sizeof(role));
    role.backpackCapacity = 20;
    role.backpackItemCount = 2;
    role.backpackItems[0].itemId = 301;
    role.backpackItems[0].seq = 18;
    role.backpackItems[0].count = 20;
    role.backpackItems[1].itemId = 301;
    role.backpackItems[1].seq = 16;
    role.backpackItems[1].count = 16;
    if (!vm_net_mock_role_consume_client_visible_stack(
            &role, 301, 18, 1, &remaining) || remaining != 19 ||
        role.backpackItemCount != 2 || role.backpackItems[0].seq != 18 ||
        role.backpackItems[0].count != 19 || role.backpackItems[1].seq != 16 ||
        role.backpackItems[1].count != 16)
    {
        fputs("direct 20+16 stack reconciliation mismatch\n", stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    const vm_net_mock_item_effect_catalog_item *smallRecovery = NULL;
    const vm_net_mock_item_effect_catalog_item *manaRecovery = NULL;
    const vm_net_mock_item_effect_catalog_item *expandCard = NULL;
    const vm_net_mock_item_effect_catalog_item *reservoirFlask = NULL;

    if (!vm_net_mock_set_resource_dir("web/fs/JHOnlineData") ||
        vm_net_mock_load_item_effect_catalog() == 0)
    {
        fputs("unable to load isolated item.dsh resource\n", stderr);
        return 1;
    }

    smallRecovery = vm_net_mock_find_item_effect_catalog_item(301);
    manaRecovery = vm_net_mock_find_item_effect_catalog_item(321);
    expandCard = vm_net_mock_find_item_effect_catalog_item(806);
    reservoirFlask = vm_net_mock_find_item_effect_catalog_item(802);
    if (smallRecovery == NULL || manaRecovery == NULL || expandCard == NULL ||
        reservoirFlask == NULL)
    {
        fputs("required item-effect rows are missing\n", stderr);
        return 1;
    }

    if (check_remote_followup_order() != 0)
        return 1;
    if (check_all_row_quantity_blob() != 0)
        return 1;
    if (check_client_stack_reconciliation() != 0)
        return 1;

    if (expect_true(vm_net_mock_item_use_uses_backpack_refresh_completion(
                        301, smallRecovery, false),
                    "301 small HP recovery") ||
        expect_true(vm_net_mock_item_use_uses_backpack_refresh_completion(
                        321, manaRecovery, false),
                    "321 small MP recovery") ||
        expect_false(vm_net_mock_item_use_uses_backpack_refresh_completion(
                         802, reservoirFlask, true),
                    "802 reservoir flask") ||
        expect_false(vm_net_mock_item_use_uses_backpack_refresh_completion(
                         806, expandCard, false),
                    "806 backpack expansion") ||
        expect_false(vm_net_mock_item_use_uses_backpack_refresh_completion(
                         VM_NET_MOCK_SMALL_HORN_ITEM_ID, NULL, false),
                    "807 remains outside the resource-derived predicate"))
    {
        return 1;
    }
    puts("normal-recovery-item-modal regression passed: 301/321 use in-place 7/4+7/11(all-rows)+7/37 completion; 802/806 retain their established contracts");
    return 0;
}
