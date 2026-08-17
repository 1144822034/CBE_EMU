/*
 * Pure regression for NPC merchant confirmation ownership and equipment-row
 * descriptions.  It does not open a listener, connect to MySQL, mutate a
 * live role, or run a client binary.
 *
 * Build from the repository root (Windows MinGW example):
 *   gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11
 *       -ffunction-sections -fdata-sections
 *       scripts/npc-equipment-confirmation-regression.c
 *       obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o
 *       obj/server/md5.o -Wl,--gc-sections -o
 *       tmp/npc-equipment-confirmation-regression.exe
 *       -lpthread -liconv -lm -lkernel32 -lws2_32
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static int expect(bool condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "npc equipment confirmation regression: %s\n", message);
    return 1;
}

static unsigned count_newlines(const char *text)
{
    unsigned count = 0;
    if (text == NULL)
        return 0;
    while (*text != '\0')
    {
        if (*text == '\n')
            ++count;
        ++text;
    }
    return count;
}

int main(void)
{
    vm_mock_service_client_session session;
    vm_net_mock_role_state role;
    vm_net_mock_role_state roleBefore;
    vm_mock_service_npc_context serviceContext;
    vm_mock_service_npc_context pageContext;
    vm_mock_service_npc_transaction_context transaction;
    vm_net_mock_equipment_catalog_item equipment;
    vm_net_mock_item_effect_catalog_item effect;
    vm_net_mock_role_state pageRole;
    char description[200];
    char effectDescription[200];
    char confirmation[256];

    memset(&session, 0, sizeof(session));
    memset(&role, 0, sizeof(role));
    memset(&serviceContext, 0, sizeof(serviceContext));
    memset(&pageContext, 0, sizeof(pageContext));
    memset(&transaction, 0, sizeof(transaction));
    memset(&equipment, 0, sizeof(equipment));
    memset(&effect, 0, sizeof(effect));
    memset(&pageRole, 0, sizeof(pageRole));
    memset(description, 0, sizeof(description));
    memset(effectDescription, 0, sizeof(effectDescription));
    memset(confirmation, 0, sizeof(confirmation));

    role.roleId = 9001;
    role.money = 12345;
    roleBefore = role;
    serviceContext.active = true;
    serviceContext.roleId = role.roleId;
    serviceContext.actorId = 20020;
    serviceContext.serviceMask =
        vm_net_mock_npc_service_kind_mask(
            VM_NET_MOCK_NPC_KIND_WEAPON_MERCHANT);
    snprintf(serviceContext.scene, sizeof(serviceContext.scene), "%s",
             "\x30\x30\xc5\xee\xc0\xb3\xcf\xc9\xb5\xba\x5f\x30\x32\x2e\x73\x63\x65"); /* 00蓬莱仙岛_02.sce */

    if (expect(vm_net_mock_npc_transaction_context_begin(
                   &session, &role, &serviceContext,
                   VM_MOCK_SERVICE_NPC_TRANSACTION_BUY, 7001, 0, 8, 1,
                   450),
               "buy prompt did not create a transaction context") ||
        expect(memcmp(&role, &roleBefore, sizeof(role)) == 0,
               "first buy click changed the role") ||
        expect(session.npcTransactionContext.active &&
                   session.npcTransactionContext.itemId == 7001 &&
                   session.npcTransactionContext.quotedPrice == 450,
               "buy prompt context is incomplete") ||
        expect(vm_net_mock_npc_transaction_context_take(
                   &session, &role, &serviceContext, &transaction),
               "matching confirm did not consume the context") ||
        expect(transaction.kind == VM_MOCK_SERVICE_NPC_TRANSACTION_BUY &&
                   transaction.selector == 8 && transaction.page == 1 &&
                   !session.npcTransactionContext.active,
               "confirm did not retain the quote or clear the context") ||
        expect(!vm_net_mock_npc_transaction_context_take(
                   &session, &role, &serviceContext, &transaction),
               "confirmation context can be replayed"))
    {
        return 1;
    }

    if (expect(vm_net_mock_npc_transaction_context_begin(
                   &session, &role, &serviceContext,
                   VM_MOCK_SERVICE_NPC_TRANSACTION_SELL, 7002, 17, 0, 0,
                   90),
               "sell prompt did not create a transaction context"))
    {
        return 1;
    }
    ++serviceContext.actorId;
    if (expect(!vm_net_mock_npc_transaction_context_take(
                   &session, &role, &serviceContext, &transaction) &&
                   !session.npcTransactionContext.active,
               "mismatched NPC context was accepted or retained"))
    {
        return 1;
    }

    /* Populate only in-memory catalog rows so the page-recovery helpers are
     * tested without loading or mutating the native NPC database. */
    g_vm_net_mock_shop_catalog_loaded = true;
    g_vm_net_mock_shop_catalog_count = 6;
    g_vm_net_mock_native_npc_db_loaded = true;
    g_vm_net_mock_native_npc_db_valid = true;
    g_vm_net_mock_npc_shop_inventory_count = 6;
    memset(g_vm_net_mock_shop_catalog, 0,
           sizeof(g_vm_net_mock_shop_catalog));
    memset(g_vm_net_mock_npc_shop_inventory, 0,
           sizeof(g_vm_net_mock_npc_shop_inventory));
    memset(&pageContext, 0, sizeof(pageContext));
    pageContext.active = true;
    pageContext.actorId = serviceContext.actorId;
    pageContext.serviceMask = serviceContext.serviceMask;
    snprintf(pageContext.scene, sizeof(pageContext.scene), "%s",
             serviceContext.scene);
    pageRole.backpackCapacity = 20;
    pageRole.backpackItemCount = 6;
    for (u32 i = 0; i < 6; ++i)
    {
        const u32 itemId = 8001u + i;
        g_vm_net_mock_shop_catalog[i].itemId = itemId;
        g_vm_net_mock_shop_catalog[i].price = 100;
        g_vm_net_mock_shop_catalog[i].isEquip = 1;
        g_vm_net_mock_shop_catalog[i].category = 7;
        snprintf(g_vm_net_mock_shop_catalog[i].name,
                 sizeof(g_vm_net_mock_shop_catalog[i].name), "item%u", i);
        snprintf(g_vm_net_mock_npc_shop_inventory[i].scene,
                 sizeof(g_vm_net_mock_npc_shop_inventory[i].scene), "%s",
                 pageContext.scene);
        g_vm_net_mock_npc_shop_inventory[i].actorId = pageContext.actorId;
        g_vm_net_mock_npc_shop_inventory[i].itemId = itemId;
        g_vm_net_mock_npc_shop_inventory[i].unitPrice = 100;
        g_vm_net_mock_npc_shop_inventory[i].enabled = true;
        pageRole.backpackItems[i].itemId = itemId;
        pageRole.backpackItems[i].seq = (u16)(i + 1u);
        pageRole.backpackItems[i].count = 1;
    }
    if (expect(vm_net_mock_npc_shop_item_page(
                   &g_vm_net_mock_shop_catalog[5], 8, &pageContext) == 1,
               "buy item page recovery did not return page two") ||
        expect(vm_net_mock_npc_sell_equipment_item_page(&pageRole, 6) == 1,
               "sell item page recovery did not return page two"))
    {
        return 1;
    }

    equipment.levelRequired = 17;
    equipment.bonus.attack = 31;
    equipment.bonus.dodge = 4;
    vm_net_mock_format_npc_equipment_instance_option_description(
        description, sizeof(description), &equipment, 5);
    vm_net_mock_append_npc_option_price_description(
        description, sizeof(description), "price:", 450);
    if (expect(strstr(description, "Lv.17") != NULL &&
                   strstr(description, "+31") != NULL &&
                   strstr(description, "+5") != NULL &&
                   strstr(description, "Lv.17\n") != NULL &&
                   strstr(description, "\\n") == NULL &&
                   count_newlines(description) >= 4 &&
                   strstr(description, "\nprice:450") != NULL,
               "equipment description omitted a line break or item data"))
    {
        return 1;
    }

    snprintf(confirmation, sizeof(confirmation), "buy-confirm:item");
    vm_net_mock_append_npc_confirmation_detail(
        confirmation, sizeof(confirmation), description);
    if (expect(strstr(confirmation, "buy-confirm:item\nLv.17") != NULL &&
                   strstr(confirmation, "\\n") == NULL &&
                   count_newlines(confirmation) >= 5 &&
                   strstr(confirmation, "\nprice:450") != NULL,
               "confirmation main text omitted the item detail or line breaks"))
    {
        return 1;
    }

    effect.levelRequired = 3;
    effect.hp = 100;
    effect.mp = 50;
    effect.durationMinutes = 10;
    vm_net_mock_format_npc_item_effect_option_description(
        effectDescription, sizeof(effectDescription), &effect);
    vm_net_mock_append_npc_option_price_description(
        effectDescription, sizeof(effectDescription), "price:", 80);
    if (expect(strstr(effectDescription, "Lv.3\n") != NULL &&
                   strstr(effectDescription, "\\n") == NULL &&
                   count_newlines(effectDescription) >= 4 &&
                   strstr(effectDescription, "\nprice:80") != NULL,
               "medicine description did not preserve multi-line detail"))
    {
        return 1;
    }

    puts("npc equipment confirmation regression passed: page recovery, one-shot context, row detail, and confirmation text");
    return 0;
}
