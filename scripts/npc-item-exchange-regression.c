/* Isolated regression for the configurable NPC item-exchange transaction.
 *
 * This uses the production in-memory role mutation and the production
 * one-shot confirmation context.  It starts no listener, does not connect to
 * MySQL, and never runs or changes the client binary.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main
#include "../src/server/mock-server.c"

static int assert_item_exchange_transaction(void)
{
    vm_net_mock_role_state role;
    vm_mock_service_client_session session;
    vm_mock_service_npc_context serviceContext;
    vm_mock_service_npc_transaction_context taken;
    vm_net_mock_npc_item_exchange_config config;
    vm_net_mock_role_state before;
    u16 inputSeq = 0;
    u16 outputSeq = 0;
    u32 inputRemaining = 0;
    vm_net_mock_backpack_item_state *output = NULL;

    memset(&role, 0, sizeof(role));
    memset(&session, 0, sizeof(session));
    memset(&serviceContext, 0, sizeof(serviceContext));
    memset(&taken, 0, sizeof(taken));
    memset(&config, 0, sizeof(config));
    role.roleId = 9001;
    role.backpackCapacity = 20;
    role.backpackItemCount = 1;
    role.nextBackpackSeq = 2;
    role.backpackItems[0].itemId = 900;
    role.backpackItems[0].seq = 1;
    role.backpackItems[0].count = 9;
    config.actorId = 30001;
    config.recipeId = 3;
    config.inputItemId = 900;
    config.inputCount = 3;
    config.outputItemId = 901;
    config.outputCount = 2;
    serviceContext.active = true;
    serviceContext.roleId = role.roleId;
    serviceContext.actorId = config.actorId;
    serviceContext.serviceMask = vm_net_mock_npc_service_kind_mask(
        VM_NET_MOCK_NPC_KIND_ITEM_EXCHANGE);
    snprintf(serviceContext.scene, sizeof(serviceContext.scene), "%s",
             "\x30\x31\xcc\xd2\xbb\xa8\xb5\xba\x5f\x30\x31\x2e\x73\x63\x65"); /* 01桃花岛_01.sce */

    if (!vm_net_mock_npc_transaction_context_begin(
            &session, &role, &serviceContext,
            VM_MOCK_SERVICE_NPC_TRANSACTION_ITEM_EXCHANGE,
            config.inputItemId, 0, config.outputItemId, config.inputCount,
            config.outputCount))
    {
        fputs("item exchange confirmation context was not created\n", stderr);
        return 1;
    }
    session.npcTransactionContext.recipeId = config.recipeId;
    if (!vm_net_mock_npc_transaction_context_take(
            &session, &role, &serviceContext, &taken) ||
        taken.kind != VM_MOCK_SERVICE_NPC_TRANSACTION_ITEM_EXCHANGE ||
        taken.recipeId != config.recipeId ||
        taken.itemId != config.inputItemId ||
        taken.selector != config.outputItemId ||
        taken.page != config.inputCount ||
        taken.quotedPrice != config.outputCount ||
        vm_net_mock_npc_transaction_context_take(
            &session, &role, &serviceContext, &taken))
    {
        fputs("item exchange confirmation context was not one-shot\n", stderr);
        return 1;
    }

    if (!vm_net_mock_role_item_exchange_in_memory(
            &role, &config, &inputSeq, &inputRemaining, &outputSeq) ||
        inputSeq == 0 || inputRemaining != 6 || outputSeq == 0 ||
        role.backpackItems[0].count != 6 ||
        (output = vm_net_mock_role_find_backpack_item(
             &role, config.outputItemId, outputSeq)) == NULL ||
        output->count != 2)
    {
        fprintf(stderr,
                "item exchange did not atomically consume and grant items input_seq=%u input_remaining=%u output_seq=%u rows=%u capacity=%u source=%u\n",
                inputSeq, inputRemaining, outputSeq, role.backpackItemCount,
                role.backpackCapacity, role.backpackItems[0].count);
        return 1;
    }

    before = role;
    config.inputCount = 99;
    if (vm_net_mock_role_item_exchange_in_memory(
            &role, &config, &inputSeq, &inputRemaining, &outputSeq) ||
        memcmp(&role, &before, sizeof(role)) != 0)
    {
        fputs("insufficient item-exchange materials changed the backpack\n", stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (assert_item_exchange_transaction() != 0)
        return 1;
    puts("npc item exchange regression passed");
    return 0;
}
