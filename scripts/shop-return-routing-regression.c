/*
 * Protocol-only regression for the shop -> backpack return path.
 *
 * Build from the repository root (Windows MinGW example):
 *   gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11
 *       -ffunction-sections -fdata-sections scripts/shop-return-routing-regression.c
 *       obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o
 *       obj/server/md5.o -Wl,--gc-sections -o
 *       tmp/shop-return-routing-regression.exe -lpthread -liconv -lm
 *       -lkernel32 -lws2_32
 *
 * The test invokes the real mock-server dispatcher in-process.  It does not
 * open a listener, connect to MySQL, inject client state, or alter a CBE/CBM
 * binary.  It also replays the ordinary mall initialization followed by its
 * actor query: 14/14,14/4,14/5,14/6 are delivered first, then 1/1/14(actorId)
 * must receive only its ActorInfo object.
 */

#include <stdio.h>
#include <string.h>

#define main cbe_server_program_main
#include "../src/server_main.c"
#undef main

static u32 append_request_object(u8 *request, u32 pos,
                                 u8 major, u8 kind, u8 subtype,
                                 const u8 *payload, u16 payloadLen)
{
    u32 objectLen = 5u + payloadLen;

    request[pos] = major;
    request[pos + 1] = kind;
    request[pos + 2] = subtype;
    request[pos + 3] = (u8)(objectLen >> 8);
    request[pos + 4] = (u8)objectLen;
    if (payloadLen != 0 && payload != NULL)
        memcpy(request + pos + 5, payload, payloadLen);
    return pos + objectLen;
}

static u32 make_items_books_request(u8 *request, const u8 *itemsPayload,
                                    u16 itemsPayloadLen)
{
    u32 pos = 4;

    request[0] = 'W';
    request[1] = 'T';
    pos = append_request_object(request, pos, 1, 17, 1,
                                itemsPayload, itemsPayloadLen);
    pos = append_request_object(request, pos, 1, 7, 42, NULL, 0);
    request[2] = (u8)(pos >> 8);
    request[3] = (u8)pos;
    return pos;
}

static u32 make_shop_catalog_request(u8 *request)
{
    u32 pos = 4;

    request[0] = 'W';
    request[1] = 'T';
    pos = append_request_object(request, pos, 1, 14, 14, NULL, 0);
    pos = append_request_object(request, pos, 1, 14, 4, NULL, 0);
    pos = append_request_object(request, pos, 1, 14, 5, NULL, 0);
    pos = append_request_object(request, pos, 1, 14, 6, NULL, 0);
    request[2] = (u8)(pos >> 8);
    request[3] = (u8)pos;
    return pos;
}

static u32 make_shop_actor_query14_request(u8 *request, u32 actorId)
{
    u8 payload[32];
    u32 payloadLen = 0;
    u32 pos = 4;

    if (!vm_net_mock_put_object_u32(payload, sizeof(payload), &payloadLen,
                                    "actorId", actorId))
    {
        return 0;
    }
    request[0] = 'W';
    request[1] = 'T';
    pos = append_request_object(request, pos, 1, 1, 14, payload,
                                (u16)payloadLen);
    request[2] = (u8)(pos >> 8);
    request[3] = (u8)pos;
    return pos;
}

static int assert_response_objects(const char *label, const u8 *response,
                                   u32 responseLen,
                                   const u8 expected[][3], u8 expectedCount)
{
    u32 offset = 5;
    u8 index = 0;

    if (response == NULL || responseLen < 5 || response[0] != 'W' ||
        response[1] != 'T' || response[4] != expectedCount)
    {
        fprintf(stderr, "%s: wrong WT header/object count\n", label);
        return 1;
    }
    while (index < expectedCount)
    {
        u32 objectLen = 0;

        if (offset + 6 > responseLen)
        {
            fprintf(stderr, "%s: truncated object at index %u\n", label, index);
            return 1;
        }
        objectLen = ((u32)response[offset + 4] << 8) | response[offset + 5];
        if (objectLen < 6 || offset + objectLen > responseLen ||
            response[offset] != expected[index][0] ||
            response[offset + 1] != expected[index][1] ||
            response[offset + 2] != expected[index][2])
        {
            fprintf(stderr, "%s: wrong object at index %u\n", label, index);
            return 1;
        }
        offset += objectLen;
        ++index;
    }
    if (offset != responseLen)
    {
        fprintf(stderr, "%s: trailing response bytes\n", label);
        return 1;
    }
    return 0;
}

static int assert_shop_actor_query_catalog_contract(void)
{
    static const u8 catalogObjects[][3] = {
        {1, 14, 14}, {1, 14, 4}, {1, 14, 5}, {1, 14, 6}
    };
    static const u8 actorOnlyObject[][3] = {{1, 1, 14}};
    static const u8 inlineObjects[][3] = {
        {1, 14, 14}, {1, 14, 4}, {1, 14, 5}, {1, 14, 6}, {1, 1, 14}
    };
    u8 catalogRequest[64];
    u8 actorRequest[64];
    u8 response[65536];
    u32 catalogRequestLen = make_shop_catalog_request(catalogRequest);
    u32 actorRequestLen = make_shop_actor_query14_request(actorRequest, 1);
    u32 responseLen;

    if (catalogRequestLen == 0 || actorRequestLen == 0)
    {
        fprintf(stderr, "shop actor query: request construction failed\n");
        return 1;
    }

    g_netMockShopCatalogDeliveredBeforeActorQuery = 0;
    g_netLastHandledValid = 0;
    g_netLastHandledSource[0] = 0;
    responseLen = vm_net_mock_build_response(catalogRequest, catalogRequestLen,
                                             response, sizeof(response));
    if (assert_response_objects("shop catalog", response, responseLen,
                                catalogObjects,
                                (u8)(sizeof(catalogObjects) / sizeof(catalogObjects[0]))) != 0 ||
        !g_netMockShopCatalogDeliveredBeforeActorQuery ||
        !g_netLastHandledValid ||
        strcmp(g_netLastHandledSource,
               "builtin-scene-interaction-followup") != 0)
    {
        fprintf(stderr, "shop catalog: builder did not arm actor-query contract\n");
        return 1;
    }

    g_netLastHandledValid = 0;
    g_netLastHandledSource[0] = 0;
    responseLen = vm_net_mock_build_response(actorRequest, actorRequestLen,
                                             response, sizeof(response));
    if (assert_response_objects("actor query after catalog", response, responseLen,
                                actorOnlyObject, 1) != 0 ||
        g_netMockShopCatalogDeliveredBeforeActorQuery ||
        g_netMockBackpackGridReseedPendingRoleId != 1 ||
        !g_netLastHandledValid ||
        strcmp(g_netLastHandledSource, "builtin-shop-actor-query14") != 0)
    {
        fprintf(stderr, "actor query after catalog: expected actorinfo-only response and role-bound grid reseed\n");
        return 1;
    }

    g_netLastHandledValid = 0;
    g_netLastHandledSource[0] = 0;
    responseLen = vm_net_mock_build_response(actorRequest, actorRequestLen,
                                             response, sizeof(response));
    if (assert_response_objects("standalone actor query", response, responseLen,
                                inlineObjects,
                                (u8)(sizeof(inlineObjects) / sizeof(inlineObjects[0]))) != 0 ||
        g_netMockBackpackGridReseedPendingRoleId != 1 ||
        !g_netLastHandledValid ||
        strcmp(g_netLastHandledSource, "builtin-shop-actor-query14") != 0)
    {
        fprintf(stderr, "standalone actor query: inline catalog fallback regressed\n");
        return 1;
    }
    g_netMockBackpackGridReseedPendingRoleId = 0;
    return 0;
}

static int assert_shop_catalog_marker_snapshot(void)
{
    vm_mock_service_account_state state;

    vm_mock_service_account_state_init(&state, "shop-return-regression");
    g_netMockShopCatalogDeliveredBeforeActorQuery = 1;
    g_netMockBackpackGridReseedPendingRoleId = 1;
    vm_mock_service_account_capture(&state);
    g_netMockShopCatalogDeliveredBeforeActorQuery = 0;
    g_netMockBackpackGridReseedPendingRoleId = 0;
    vm_mock_service_account_restore(&state);
    if (!g_netMockShopCatalogDeliveredBeforeActorQuery ||
        g_netMockBackpackGridReseedPendingRoleId != 1)
    {
        fprintf(stderr, "shop catalog marker: account snapshot lost state\n");
        return 1;
    }
    vm_mock_service_account_restore(NULL);
    g_netMockShopCatalogDeliveredBeforeActorQuery = 0;
    g_netMockBackpackGridReseedPendingRoleId = 0;
    return 0;
}

static bool response_has_object(const u8 *response, u32 responseLen,
                                u8 major, u8 kind, u8 subtype)
{
    u32 offset = 5;
    u8 objectCount = 0;

    if (response == NULL || responseLen < 5 || response[0] != 'W' ||
        response[1] != 'T')
    {
        return false;
    }
    objectCount = response[4];
    while (objectCount-- != 0)
    {
        u32 objectLen = 0;
        if (offset + 6 > responseLen)
            return false;
        objectLen = ((u32)response[offset + 4] << 8) |
                    response[offset + 5];
        if (objectLen < 6 || offset + objectLen > responseLen)
            return false;
        if (response[offset] == major && response[offset + 1] == kind &&
            response[offset + 2] == subtype)
        {
            return true;
        }
        offset += objectLen;
    }
    return false;
}

static int assert_shop_return_grid_reseed_contract(void)
{
    vm_net_mock_role_state *role = NULL;
    vm_net_mock_role_state savedRole;
    u8 response[65536];
    u32 responseLen = 5;
    u8 responseObjectCount = 0;
    u32 savedRoleCount = g_vm_net_mock_role_db.roleCount;
    u32 savedActiveRoleId = g_vm_net_mock_role_db.activeRoleId;
    bool savedRoleDbLoaded = g_vm_net_mock_role_db_loaded;
    bool savedRoleDbValid = g_vm_net_mock_role_db_valid;

    memset(&savedRole, 0, sizeof(savedRole));
    savedRole = g_vm_net_mock_role_db.roles[0];
    memset(&g_vm_net_mock_role_db.roles[0], 0,
           sizeof(g_vm_net_mock_role_db.roles[0]));
    g_vm_net_mock_role_db.roleCount = 1;
    g_vm_net_mock_role_db.activeRoleId = 1;
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = true;
    role = &g_vm_net_mock_role_db.roles[0];
    role->roleId = 1;
    role->backpackCapacity = 20;
    role->backpackItemCount = 1;
    role->nextBackpackSeq = 2;
    role->backpackItems[0].itemId = 800;
    role->backpackItems[0].seq = 1;
    role->backpackItems[0].count = 5;

    g_netMockBackpackGridSeededRoleId = 1;
    g_netMockBackpackGridReseedPendingRoleId = 1;
    if (!vm_net_mock_append_backpack_role_grid_main_objects(
            response, sizeof(response), &responseLen, &responseObjectCount))
    {
        fprintf(stderr, "shop return grid: bootstrap builder failed\n");
        goto fail;
    }
    vm_net_mock_finish_wt_packet(response, responseLen, responseObjectCount);
    if (responseObjectCount == 0 ||
        !response_has_object(response, responseLen, 1, 30, 21) ||
        g_netMockBackpackGridReseedPendingRoleId != 0 ||
        g_netMockBackpackGridSeededRoleId != 1)
    {
        fprintf(stderr, "shop return grid: same-role bootstrap did not consume reseed\n");
        goto fail;
    }

    g_vm_net_mock_role_db.roles[0] = savedRole;
    g_vm_net_mock_role_db.roleCount = savedRoleCount;
    g_vm_net_mock_role_db.activeRoleId = savedActiveRoleId;
    g_vm_net_mock_role_db_loaded = savedRoleDbLoaded;
    g_vm_net_mock_role_db_valid = savedRoleDbValid;
    g_netMockBackpackGridReseedPendingRoleId = 0;
    return 0;

fail:
    g_vm_net_mock_role_db.roles[0] = savedRole;
    g_vm_net_mock_role_db.roleCount = savedRoleCount;
    g_vm_net_mock_role_db.activeRoleId = savedActiveRoleId;
    g_vm_net_mock_role_db_loaded = savedRoleDbLoaded;
    g_vm_net_mock_role_db_valid = savedRoleDbValid;
    g_netMockBackpackGridReseedPendingRoleId = 0;
    return 1;
}

static int assert_dispatch_matches_builder(const char *label,
                                            const u8 *request, u32 requestLen,
                                            bool shopResponse,
                                            const char *expectedSource)
{
    u8 expected[65536];
    u8 actualPending[65536];
    u8 actualClear[65536];
    char pendingSource[sizeof(g_netLastHandledSource)];
    char clearSource[sizeof(g_netLastHandledSource)];
    u32 expectedLen = shopResponse
        ? vm_net_mock_build_shop_items_books_combo_response(
              request, requestLen, expected, sizeof(expected))
        : vm_net_mock_build_backpack_items_books_combo_response(
              request, requestLen, expected, sizeof(expected));
    u32 pendingLen;
    u32 clearLen;

    if (expectedLen == 0)
    {
        fprintf(stderr, "%s: reference builder returned no response\n", label);
        return 1;
    }

    g_netMockShop17ListPending = 1;
    g_netLastHandledValid = 0;
    g_netLastHandledSource[0] = 0;
    pendingLen = vm_net_mock_build_response(request, requestLen,
                                            actualPending, sizeof(actualPending));
    snprintf(pendingSource, sizeof(pendingSource), "%s", g_netLastHandledSource);
    g_netMockShop17ListPending = 0;
    g_netLastHandledValid = 0;
    g_netLastHandledSource[0] = 0;
    clearLen = vm_net_mock_build_response(request, requestLen,
                                          actualClear, sizeof(actualClear));
    snprintf(clearSource, sizeof(clearSource), "%s", g_netLastHandledSource);

    if (pendingLen != expectedLen || clearLen != expectedLen ||
        memcmp(actualPending, expected, expectedLen) != 0 ||
        memcmp(actualClear, expected, expectedLen) != 0 ||
        !g_netLastHandledValid ||
        strcmp(pendingSource, expectedSource) != 0 ||
        strcmp(clearSource, expectedSource) != 0)
    {
        fprintf(stderr,
                "%s: dispatcher route differs by stale pending state "
                "(expected=%u pending=%u/%s clear=%u/%s)\n",
                label, expectedLen, pendingLen, pendingSource,
                clearLen, clearSource);
        return 1;
    }
    return 0;
}

int main(void)
{
    static const u8 shopPayload[11] = {
        0x00, 0x09, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x02, 0x00, 0x01, 0x00
    };
    u8 shopRequest[64];
    u8 backpackRequest[64];
    u32 shopRequestLen = make_items_books_request(
        shopRequest, shopPayload, sizeof(shopPayload));
    u32 backpackRequestLen = make_items_books_request(
        backpackRequest, NULL, 0);

    /* The dispatcher route is under test, not persistence. */
    g_vm_net_mock_shop_admin_db_loaded = true;
    g_vm_net_mock_shop_admin_db_valid = false;
    g_vm_net_mock_role_db_loaded = true;
    g_vm_net_mock_role_db_valid = false;

    if (assert_dispatch_matches_builder(
            "non-empty 17/1 shop combo", shopRequest, shopRequestLen, true,
            "builtin-shop-items-books-combo") != 0 ||
        assert_dispatch_matches_builder(
            "empty 17/1 backpack combo", backpackRequest, backpackRequestLen, false,
            "builtin-backpack-items-books-combo") != 0 ||
        assert_shop_actor_query_catalog_contract() != 0 ||
        assert_shop_catalog_marker_snapshot() != 0 ||
        assert_shop_return_grid_reseed_contract() != 0)
    {
        return 1;
    }

    puts("shop-return regression passed: backpack routing and one-shot shop actor-query catalog contract");
    return 0;
}
